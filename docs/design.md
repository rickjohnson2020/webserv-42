# Webserv 設計書 / Design Document — Mandatory Part 完全版

**C++98 / poll() / HTTP-1.1 / 2人チーム構成**
42 School — Webserv (v24.0)

> 前提となる技術選択: poll() を採用 / Mac 環境 (poll は Mac・Linux 両対応) / HTTP/1.1 を目標 / Bonus (cookie・session・複数CGI) は対象外

---

## 目次

0. [この設計書の読み方](#0-この設計書の読み方)
1. [プロジェクト概要と必須要件](#1-プロジェクト概要と必須要件)
2. [全体アーキテクチャ](#2-全体アーキテクチャ)
3. [設定層の設計](#3-設定層の設計)
4. [I/O 層の設計](#4-io-層の設計)
5. [HTTP 層の設計](#5-http-層の設計)
6. [CGI 層の設計 (I/O 層との接点)](#6-cgi-層の設計-io-層との接点)
7. [モジュール間の境界面 (契約)](#7-モジュール間の境界面-契約)
8. [接続の状態遷移 (CGI 込み)](#8-接続の状態遷移-cgi-込み)
9. [実装順序と分担](#9-実装順序と分担)
10. [ファイル構成とクラス一覧](#10-ファイル構成とクラス一覧)
11. [テスト方針と提出物](#11-テスト方針と提出物)

---

## 0. この設計書の読み方

本書は Webserv の Mandatory Part 全体を、2人で開発に着手できるレベルまで具体化したものである。各クラスの責務・公開インターフェイス・状態遷移・モジュール間の境界面を明示し、担当の切れ目を明確にしている。

本書に出てくるクラス名・関数シグネチャは設計の意図を伝えるための指針であり、実装時に細部を調整してよい。ただし「モジュール間の境界面 (第7章)」だけは、2人が独立に作業するための契約なので、変更するときは必ず相互に合意すること。

> **✅ スコープの確認**
> - 本書は Mandatory Part のみを対象とする。Bonus (cookie / session 管理、複数 CGI タイプ) は含めない。
> - ただし将来 Bonus を足しても破綻しないよう、CGI まわりは拡張しやすい形にしてある。

---

## 1. プロジェクト概要と必須要件

Webserv は C++98 で HTTP/1.1 サーバーを自作するプロジェクトである。実ブラウザからアクセスでき、NGINX の server ブロックに着想を得た設定ファイルで挙動を制御する。起動方法:

```
./webserv [configuration file]
```

### 1.1 Mandatory で実装する機能の一覧

本設計書がカバーする実装対象を、要件を機能に翻訳して列挙する。これが「やることリスト」の総体である。

| 機能カテゴリ | 具体的にやること | 主担当 |
|---|---|---|
| 設定ファイル | 引数または既定パスから .conf を読み、複数 server / location を構造体化する | A |
| イベントループ | 単一 poll() で listen ソケットと全クライアントを駆動、読み書き同時監視 | A |
| ノンブロッキング I/O | 全ソケット・パイプを O_NONBLOCK 化、readiness 通知後のみ read/write | A |
| 複数ポート listen | 設定の各 listen 分だけ listen ソケットを作り 1 つの poll に載せる | A |
| 接続管理・タイムアウト | Connection ごとの状態保持、切断処理、無通信接続の打ち切り | A |
| HTTP リクエスト解析 | 逐次パース、メソッド/パス/ヘッダ/ボディ、chunked の un-chunk | B |
| ルーティング | パスから最長一致の location を決定し設定を適用 | B |
| GET / 静的配信 | ファイル配信、index、autoindex (ディレクトリ一覧) | B |
| POST / アップロード | multipart/form-data 等を受け取り保存先へ書き出す | B |
| DELETE | 指定リソースの削除 | B |
| パストラバーサル防止 | パスを正規化し、解決先が root の外に出るアクセスを 403 で拒否 | B |
| HTTP レスポンス生成 | 正確なステータスコード、ヘッダ整合、Content-Length | B |
| エラーページ | 設定があれば使い、無ければ内蔵デフォルトを返す | B |
| HTTP リダイレクト | location の return 指定で 3xx を返す | B |
| CGI 実行 | 拡張子判定、環境変数構築、fork+execve、パイプを poll に統合、タイムアウト時 kill | A+B |

### 1.2 採点 0 になる絶対ルール

> **🚫 これを破ると即 0 点**
> - readiness (poll の通知) を待たずにソケット/パイプへ read・recv・write・send した瞬間に 0 点。
> - read/write の後で errno を見てサーバー挙動を変えるのは禁止。判断は戻り値と poll 通知のみ。
> - プログラムのクラッシュ・異常終了 (メモリ不足含む) は 0 点。全例外を捕捉する。
> - 別の web サーバーを execve して仕事を肩代わりさせるのは禁止。
> - fork は CGI 以外の用途で使用禁止。

通常のディスクファイル (要求された HTML・画像など) は「待たされない」ため上記の readiness ルールの例外で、poll を通さず直接 read/write してよい。待たされるのはソケットと CGI のパイプだけである。

---

## 2. 全体アーキテクチャ

サーバーを4層に分離する。各層は下位層のインターフェイスのみに依存し、上位の詳細を知らない。これが2人の並行作業の境界面になり、ノンブロッキングの規律を I/O 層に閉じ込める。

| 層 | 責務 | 主なクラス |
|---|---|---|
| 設定層 | .conf のパース・検証、ルックアップ用データ構造の保持 | Config, ServerConfig, LocationConfig, ConfigParser |
| I/O 層 | ソケット生成・listen・accept、単一イベントループ、fd 登録/解除、ノンブロッキング送受信、タイムアウト | EventLoop, ListenSocket, Connection |
| HTTP 層 | リクエスト逐次パース、ルーティング、レスポンス生成、静的配信、アップロード、DELETE | HttpRequest, HttpResponse, Router, RequestHandler |
| CGI 層 | 拡張子判定、環境変数構築、fork+execve、パイプ I/O、un-chunk 連携 | CgiHandler, CgiProcess |

### 2.1 基本のデータフロー (静的リクエスト)

1. EventLoop が poll() で全 fd を監視する。
2. listen fd が読める → accept() して Connection を生成、client fd を EventLoop に登録 (POLLIN)。
3. client fd が読める → Connection が recv し、HttpRequest にバイトを供給 (逐次パース)。
4. リクエスト完成 → Router が Config を引いて location を決定、RequestHandler が処理。
5. 静的処理ならその場で HttpResponse 完成 → Connection の送信バッファへ、POLLOUT を立てる。
6. client fd が書ける → 送信バッファを send。送り切ったら keep-alive 判定 (READING へ戻るか CLOSING)。

### 2.2 CGI を含むデータフロー (非同期経路)

CGI はパイプという「待たされる fd」を扱うため、その場で結果を読み切る同期処理にできない。結果は後から poll 経由で届く。

1. リクエスト完成 → RequestHandler が CGI 対象と判定 → CgiProcess を起動 (fork+execve)。
2. CGI のパイプ fd をノンブロッキング化し EventLoop に登録。Connection は CGI_RUNNING 状態へ。
3. RequestHandler は「CGI 起動した、結果は後で」を呼び出し側に伝えて一旦抜ける。
4. poll が CGI パイプの readiness を通知 → CgiProcess が入力を書き/出力を読み進める。
5. CGI 出力が EOF (read が 0) → 出力からレスポンスを組み立て、対応する Connection の送信バッファへ。
6. 以降は静的と同じく POLLOUT で送信。

> **✅ 層分離の鉄則**
> - I/O 層は「いつ読めるか/書けるか」だけを管理し、HTTP の意味を一切知らない。
> - HTTP 層は「バイト列をもらってバイト列を返す」だけで、ソケットを直接触らない。
> - CGI のパイプを poll に載せる部分だけが I/O 層と CGI 層の唯一の接点 (第6章で詳述)。

---

## 3. 設定層の設計

ConfigParser が .conf を読み、Config (= 複数 ServerConfig) を構築する。各 ServerConfig は複数の LocationConfig を持つ。I/O 層・HTTP 層から参照される不変データなので、起動時に確定させ以後変更しない。

### 3.1 データ構造

```cpp
struct LocationConfig {
  std::string               path;            // 例: "/", "/upload", "/cgi-bin"
  std::set<std::string>     allowedMethods;  // GET/POST/DELETE のうち許可分
  std::string               root;            // パスのマッピング先ディレクトリ
  std::string               index;           // ディレクトリ要求時の既定ファイル
  bool                      autoindex;       // ディレクトリ一覧の on/off
  std::pair<int,std::string> redirect;       // return: コード+URL (無ければ code=0)
  std::string               uploadStore;     // アップロード保存先
  std::map<std::string,std::string> cgi;     // 拡張子 -> インタプリタのパス
};

struct ServerConfig {
  std::string               host;            // 例: "0.0.0.0"
  int                       port;            // 例: 8080
  std::map<int,std::string> errorPages;      // ステータス -> ファイルパス
  size_t                    clientMaxBodySize;
  std::vector<LocationConfig> locations;
};

class Config {
  std::vector<ServerConfig> _servers;
public:
  const std::vector<ServerConfig>& servers() const;
  // host:port から担当 ServerConfig を引く (複数ポート対応の要)
  const ServerConfig* findServer(const std::string& host, int port) const;
};
```

### 3.2 設定ファイルの書式 (例)

NGINX 風の簡易書式を採用する。パーサが解釈すべき最小の例を示す。これをデモ用 .conf の出発点にする。

```nginx
server {
    listen 0.0.0.0:8080;
    client_max_body_size 10M;
    error_page 404 ./www/errors/404.html;

    location / {
        root ./www/site;
        index index.html;
        allowed_methods GET;
        autoindex off;
    }
    location /upload {
        root ./www/uploads;
        allowed_methods GET POST DELETE;
        upload_store ./www/uploads;
    }
    location /cgi-bin {
        root ./www/cgi-bin;
        allowed_methods GET POST;
        cgi .py /usr/bin/python3;
    }
}
```

### 3.3 パーサが守ること

- 不正な設定 (未知のディレクティブ、必須項目欠落、ポート重複など) は起動時にエラーで終了し、サーバーを起動しない。実行中ではなく起動時の検証で弾く。
- client_max_body_size の M/K といった単位を数値に正規化する。
- location は path の最長一致でルックアップできるよう保持する (Router が使う)。
- 引数で .conf が渡されなければ既定パス (例: ./config/default.conf) を読む。

---

## 4. I/O 層の設計

### 4.1 EventLoop

プログラム全体で唯一の poll() を保持する中心クラス。全 fd の pollfd 配列を管理し、イベントを各 ListenSocket / Connection / CgiProcess へディスパッチする。

```cpp
class EventLoop {
  std::vector<struct pollfd>      _pfds;
  std::map<int, ListenSocket*>    _listeners;
  std::map<int, Connection*>      _conns;
  std::map<int, CgiProcess*>      _cgis;     // CGI パイプ fd -> CgiProcess
public:
  void addListener(ListenSocket* ls);
  void registerFd(int fd, short events);     // POLLIN / POLLOUT
  void modifyEvents(int fd, short events);
  void unregisterFd(int fd);
  void attachCgi(int pipeFd, CgiProcess* p); // CGI パイプを載せる (第6章)
  void run();                                // メインループ
};
```

POLLOUT の付け外しの規律: 送信待ちデータがある間だけ `POLLOUT` を立て、送り切ったら下ろす。常時立てると poll が空回りして CPU を浪費する。

### 4.2 ListenSocket

1 つの host:port に対応する待ち受けソケット。socket → setsockopt(SO_REUSEADDR) → fcntl(O_NONBLOCK) → bind → listen で準備し、読めるイベントで accept() する。設定の listen の数だけ生成する (複数ポート対応)。

> **ℹ️ Mac 環境での注意**
> - accept4 や SOCK_NONBLOCK は Linux 専用で Mac に無い。ソケットは accept() 後に fcntl(fd, F_SETFL, O_NONBLOCK) で明示的にノンブロッキング化する。
> - 使ってよい fcntl フラグは F_SETFL / O_NONBLOCK / FD_CLOEXEC のみ。この書き方は Linux でもそのまま動く。

### 4.3 Connection

1 クライアント接続の状態を保持する。受信/送信バッファ、パース中の HttpRequest、最終アクティブ時刻、所属 ServerConfig を持ち、自分の状態機械を進める。

```cpp
enum ConnState {
  READING,       // リクエスト受信中
  HANDLING,      // 同期処理中 (すぐ WRITING へ)
  CGI_RUNNING,   // CGI の結果を待っている (非同期)
  WRITING,       // レスポンス送信中
  CLOSING        // 切断処理中
};

class Connection {
  int                _fd;
  ConnState          _state;
  std::string        _inBuf;        // 受信した生バイト
  std::string        _outBuf;       // 送信待ちバイト
  HttpRequest        _req;
  const ServerConfig* _server;      // この接続を受けた listen に対応
  time_t             _lastActive;
public:
  void onReadable();                // recv -> _req に供給 -> 完成なら処理へ
  void onWritable();                // _outBuf を send
  void onCgiDone(const HttpResponse& r); // CGI 完了時に呼ばれる (第6章)
  bool isTimedOut(time_t now) const;
};
```

### 4.4 タイムアウトとハング防止

- 全 Connection に最終アクティブ時刻を持たせ、メインループ毎に経過時間を走査する。
- 一定時間 (例: 30〜60秒) 進展のない接続は 408 を返すか即 CLOSING にして資源を解放する。
- poll() のタイムアウト引数を有限値にし、イベントが無くてもタイムアウト掃除が定期的に回るようにする。
- 「リクエストが永遠にハングしない」要件はこの掃除で担保する。

---

## 5. HTTP 層の設計

### 5.1 HttpRequest (逐次パーサ)

受信バイト列を少しずつ受け取り、途中状態でも破綻せずパースを進める。これがノンブロッキング設計の要点。データが部分的にしか来なくても consume() を繰り返し呼べば最終的に完成する。

```cpp
enum ParseState {
  PARSE_REQUEST_LINE,   // メソッド・ターゲット・バージョン行
  PARSE_HEADERS,        // ヘッダ群
  PARSE_BODY,           // Content-Length ベースの本文
  PARSE_CHUNKED,        // Transfer-Encoding: chunked の本文
  PARSE_COMPLETE,       // 完成
  PARSE_ERROR           // 不正リクエスト (400 など)
};

class HttpRequest {
  Method                              _method;   // GET/POST/DELETE
  std::string                         _target;   // 生のリクエストターゲット
  std::string                         _path;     // デコード済みパス
  std::string                         _query;    // クエリ文字列 (CGI 用)
  std::map<std::string,std::string>   _headers;
  std::string                         _body;     // un-chunk 済みの本文
  ParseState                          _state;
public:
  void       consume(const std::string& bytes); // 受け取るたびに状態を進める
  bool       isComplete() const;
  bool       hasError() const;
  // アクセサ: method(), path(), query(), header(name), body() ...
};
```

> **⚠️ パースで守ること**
> - Transfer-Encoding: chunked のボディは必ず un-chunk して _body に格納する (CGI は un-chunk 済みを期待)。
> - Content-Length と実ボディ長の整合、ヘッダ過大・行過長などの異常は PARSE_ERROR にして対応するステータスを返す。
> - client_max_body_size 超過は 413 Payload Too Large。
> - HTTP/1.1 では Host ヘッダ必須。欠落は 400。

### 5.2 Router

完成した HttpRequest のパスから、所属 ServerConfig の中で最長一致する LocationConfig を選ぶ。正規表現は不要 (要件)。選んだ location を RequestHandler に渡す。

```cpp
class Router {
public:
  // 一致する location が無ければ NULL (404 相当)
  static const LocationConfig* match(const ServerConfig& srv,
                                     const std::string& path);
};
```

> **🚫 match() が NULL を返したとき (必ず処理する)**
> - 設定に location / (ルート) を必ず置けば最長一致で最低でも / に当たり、通常 NULL にはならない。デモ用 .conf では / を用意することを推奨する。
> - ただし / の無い設定も書けてしまうため、NULL が返ったら 404 を返す防御を必ず入れる。NULL を * でデリファレンスするとクラッシュ (= 採点 0) になるので、呼び出し側で NULL チェックする (第7.1章の結合コード参照)。

### 5.3 RequestHandler

メソッドごとの処理を実装する中心。静的処理はその場で HttpResponse を完成させ、CGI 対象なら CgiProcess を起動して「処理中」を返す。この二択を戻り値で表現する。

```cpp
enum HandleResult {
  RESPONSE_READY,   // out にレスポンスが入った (同期完了)
  CGI_STARTED       // CGI を起動した。結果は後から poll 経由で届く
};

class RequestHandler {
public:
  HandleResult handle(const HttpRequest& req,
                      const LocationConfig& loc,
                      const ServerConfig& srv,
                      Connection& conn,      // CGI 起動時の紐付けに使う
                      HttpResponse& out);    // RESPONSE_READY 時に書き込む
private:
  HandleResult doGet(...);     // 静的配信 / index / autoindex / CGI 振り分け
  HandleResult doPost(...);    // アップロード / CGI
  HandleResult doDelete(...);  // リソース削除
  HttpResponse  makeRedirect(const LocationConfig& loc);
  HttpResponse  makeError(int code, const ServerConfig& srv);
};
```

処理の前段で、location の allowedMethods に当該メソッドが無ければ 405 を返す。redirect 指定があれば 3xx を返す。これらは doGet 等より先に判定する。

> **🚫 前段チェックで守ること**
> - 405 を返すときは Allow ヘッダを必ず付ける (HTTP 仕様の要求)。値は location の allowedMethods をカンマ区切りにしたもの。例: `Allow: GET, POST`。NGINX も付けてくるので挙動を揃える。
> - パス解決では必ずパストラバーサル対策を行う。root + path を素朴に連結すると `GET /../../etc/passwd` のようなパスで root の外のファイルを読まれる脆弱性 (パストラバーサル) が生じる。パスを正規化 (. や .. を解決) し、解決後の絶対パスが root ディレクトリ内に収まることを検証し、外に出るなら 403 を返す。全メソッド (GET/POST/DELETE) のパス解決に共通で適用する。

#### 5.3.1 メソッド別の中身

| メソッド | やること |
|---|---|
| GET | root とパスからファイル実体を解決。ファイルなら読んで 200 で返す。ディレクトリなら index を試し、無ければ autoindex が on なら一覧 HTML を生成、off なら 403。存在しなければ 404。拡張子が cgi 対象なら CGI へ。 |
| POST | upload_store があればボディを保存先に書き出す (アップロード)。拡張子が cgi 対象なら un-chunk 済みボディを CGI の stdin へ渡して実行。 |
| DELETE | root とパスから対象を解決し、存在すれば削除して 204/200、無ければ 404、権限不可なら 403。 |

### 5.4 HttpResponse

ステータスライン・ヘッダ・ボディを組み立て、最終バイト列にシリアライズする。Content-Length と Connection ヘッダの整合を担保する。

```cpp
class HttpResponse {
  int                                 _status;   // 200, 404 ...
  std::map<std::string,std::string>   _headers;
  std::string                         _body;
public:
  void setStatus(int code);
  void setHeader(const std::string& k, const std::string& v);
  void setBody(const std::string& b);            // Content-Length も自動設定
  std::string serialize() const;                 // 送信用バイト列
};
```

### 5.5 ステータスコードとエラーページ

要件「ステータスコードは正確に」を満たすため、主要コードの使い分けを統一する。

| コード | 意味 | 発生場面 |
|---|---|---|
| 200 | OK | GET 成功、CGI 正常出力 |
| 201/204 | 作成/内容なし | アップロード成功 / DELETE 成功 |
| 301/302 | リダイレクト | location の return 指定 |
| 400 | Bad Request | パース不正、Host 欠落 |
| 403 | Forbidden | autoindex off のディレクトリ、権限不可 |
| 404 | Not Found | ファイル/location 不在 |
| 405 | Method Not Allowed | allowedMethods 外のメソッド (Allow ヘッダ必須) |
| 413 | Payload Too Large | client_max_body_size 超過 |
| 500 | Internal Server Error | CGI 異常、サーバー内部の失敗 |
| 501 | Not Implemented | 未対応メソッド |
| 504 | Gateway Timeout | CGI がタイムアウトし強制終了されたとき |

各コードに対し、設定に error_page があればそれを返し、無ければサーバー内蔵のデフォルト HTML を返す。デフォルトは「コード番号 + 短い説明」の最小限の HTML で構わない。

---

## 6. CGI 層の設計 (I/O 層との接点)

CGI は本プロジェクトで唯一、同期処理に収まらない部分。パイプという「待たされる fd」を扱うため、結果を読み切るまで待つのではなく、パイプを poll に載せて非同期に処理する。ここが I/O 層 (担当 A) と CGI 層 (担当 B) の唯一の接点であり、第7章の境界面合意の最重要項目。

### 6.1 CgiProcess

```cpp
enum CgiState {
  CGI_WRITING_INPUT,   // POST ボディを CGI の stdin に書いている
  CGI_READING_OUTPUT,  // CGI の stdout から結果を読んでいる
  CGI_DONE,            // 完了 -> レスポンス組み立てへ
  CGI_FAILED           // 異常 -> 500
};

class CgiProcess {
  pid_t        _pid;
  int          _inFd;        // CGI の stdin へ書く側 (POST ボディ)
  int          _outFd;       // CGI の stdout から読む側
  std::string  _inBuf;       // 未送信のリクエストボディ
  std::string  _outBuf;      // CGI からの出力蓄積
  CgiState     _state;
  Connection*  _owner;       // どのクライアント接続のための CGI か
  time_t       _startTime;   // 起動時刻 (タイムアウト判定用)
public:
  bool start(const HttpRequest& req, const LocationConfig& loc); // fork+execve
  void onWritable();         // _inBuf を stdin パイプへ書く
  void onReadable();         // stdout パイプから _outBuf へ読む
  bool isTimedOut(time_t now) const;  // 起動から一定時間で true
  void killProcess();        // SIGKILL で強制終了し回収
  HttpResponse buildResponse() const; // CGI 出力 -> HttpResponse
};
```

### 6.2 起動から完了までの流れ

1. RequestHandler が cgi 対象と判定 → CgiProcess を生成し start() を呼ぶ。
2. start(): pipe() を 2 本作り、fork()。子は dup2 で stdin/stdout をパイプに繋ぎ、対象ディレクトリへ chdir してから execve でインタプリタを起動。親は使わない側を close。
3. 親側の inFd / outFd を fcntl で O_NONBLOCK 化し、EventLoop::attachCgi() で poll に登録。Connection は CGI_RUNNING へ。
4. POST ボディがあれば、poll が inFd の POLLOUT を通知するたびに onWritable() で少しずつ書く。書き切ったら inFd を close (CGI に EOF を伝える)。
5. poll が outFd の POLLIN を通知するたびに onReadable() で _outBuf に蓄積。read が 0 (EOF) になったら CGI_DONE。
6. buildResponse() で出力をパースしてレスポンス化し、_owner->onCgiDone() で Connection に渡す。Connection は WRITING へ遷移し送信を始める。
7. waitpid(pid, &status, WNOHANG) で子を回収する。まだ終了していなければ (戻り値 0) 次のループで再試行し、ブロックしない。回収後に CGI のパイプ fd を EventLoop から unregister。

### 6.3 CGI のタイムアウト (ハング防止)

CGI スクリプトが無限ループや無応答に陥ると、サーバーが永遠に結果を待ちハングする。これは「リクエストは永遠にハングしてはならない」要件に違反する。クライアント接続のタイムアウト掃除 (第4.4章) とは別に、CGI プロセス自体のタイムアウトを設ける。

1. CgiProcess は起動時刻 (_startTime) を記録する。
2. メインループの定期チェックで、各 CGI が一定時間 (例: 5〜10 秒) を超えても完了しなければ isTimedOut() が true になる。
3. タイムアウトしたら killProcess() で kill(pid, SIGKILL) → waitpid で回収し、パイプ fd を unregister する。
4. クライアントには 504 Gateway Timeout を返す (_owner->onCgiDone に 504 レスポンスを渡す)。

> **🚫 CGI で守ること**
> - fork は CGI のためだけに使う (他用途は禁止)。
> - CGI のパイプも必ず単一 poll の管理対象にし、同期 read/write でブロックしない。
> - waitpid は WNOHANG 付きで呼びブロックを避ける。子がまだ終わっていなければ次のループで再回収する。
> - CGI プロセスのタイムアウトを設け、超過したら SIGKILL で強制終了し 504 を返す (第6.3章)。
> - chunked リクエストは un-chunk 済みのボディを stdin へ流し、書き切ったら close で EOF を示す。
> - CGI が Content-Length を返さない場合は出力 EOF を本文終端とみなす。
> - CGI は対象スクリプトのディレクトリで実行する (chdir) ことで相対パス参照を正しくする。
> - 環境変数: REQUEST_METHOD, CONTENT_LENGTH, CONTENT_TYPE, PATH_INFO, QUERY_STRING, SERVER_PROTOCOL など必要分を構築する。

---

## 7. モジュール間の境界面 (契約)

ここは2人が独立に作業するための契約。着手前に必ず合意し、変更時は相互承認する。これが揃えば、あとは別ファイルで並行開発できる。

> **ℹ️ 共有ヘッダ webserv.hpp について**
> - 本章の境界面は、別ファイル webserv.hpp (チーム共有インターフェイス) に、全クラスの宣言と「誰がどの関数を呼ぶか」のコメント付きで具体化してある。着手前に2人でこのファイルを読み、形に合意してから各自の実装を始める。
> - これはたたき台であり、実装を進める中で引数・戻り値・メンバの持ち方に調整が入るのは想定内。変更したら必ず相方に共有し、双方が同じ webserv.hpp を更新すること。設計書のクラス図と webserv.hpp が食い違ったら、実装が進んでいる webserv.hpp 側を正とする。

### 7.1 Connection → HTTP 層

Connection はリクエストが完成したら、次の形で HTTP 層を呼ぶ。戻り値で同期完了か CGI 起動かを受け取り、自分の状態を更新する。

```cpp
// Connection::onReadable() の中、_req.isComplete() の後
HttpResponse resp;
const LocationConfig* loc = Router::match(*_server, _req.path());
if (loc == NULL) {                      // 一致 location 無し -> 404
    _outBuf = handler.makeError(404, *_server).serialize();
    _state  = WRITING;
    loop.modifyEvents(_fd, POLLOUT);
    return;
}
HandleResult r = handler.handle(_req, *loc, *_server, *this, resp);
if (r == RESPONSE_READY) {
    _outBuf = resp.serialize();
    _state  = WRITING;
    loop.modifyEvents(_fd, POLLOUT);
} else { // CGI_STARTED
    _state  = CGI_RUNNING;   // 送信は CGI 完了後 (onCgiDone)
}
```

### 7.2 CGI 層 → I/O 層 (最重要)

CGI 完了時、CgiProcess は Connection に結果を返し、Connection が送信フェーズに入る。CGI のパイプ fd の poll 登録/解除は EventLoop が担う。

```cpp
// CgiProcess が CGI_DONE になったとき
_owner->onCgiDone(buildResponse());

// Connection::onCgiDone(const HttpResponse& r)
_outBuf = r.serialize();
_state  = WRITING;
loop.modifyEvents(_fd, POLLOUT);
```

> **ℹ️ ここで2人が必ず合意すべき4点**
> 1. handle() の正確なシグネチャと、CGI 起動時に Connection をどう渡すか。
> 2. CGI のパイプ fd を EventLoop に登録/解除する関数名と呼ぶタイミング (誰が呼ぶか)。
> 3. CGI 完了を Connection に伝えるコールバック (onCgiDone) の形。
> 4. Config のルックアップ API (findServer / Router::match) の形。

### 7.3 設定層 → 全層

Config は起動時に確定する不変データ。I/O 層は host:port から findServer で ServerConfig を引き、HTTP 層は Router::match で LocationConfig を引く。両者が同じ構造体を読むだけなので、構造体定義 (第3.1章) を最初に固定する。

---

## 8. 接続の状態遷移 (CGI 込み)

各 Connection は次の状態機械で進む。poll のイベントを受けるたびに1ステップだけ進め、決してブロックしない。

| 状態 | 監視イベント | 遷移・動作 |
|---|---|---|
| READING | POLLIN | recv -> HttpRequest.consume()。完成 → handle() を呼ぶ。同期完了なら WRITING、CGI なら CGI_RUNNING。パース不正は対応エラーを組んで WRITING |
| HANDLING | (内部) | 同期処理の一瞬の状態。実装上は READING から直接 WRITING へ飛ばしてもよい |
| CGI_RUNNING | (CGI の fd 側) | クライアント fd は待機。CGI パイプの poll イベントで CgiProcess が進行。onCgiDone で WRITING へ |
| WRITING | POLLOUT | _outBuf を send。送り切ったら keep-alive なら READING に戻り _req をリセット、close 指定なら CLOSING |
| CLOSING | — | fd を EventLoop から unregister し close。Connection を破棄 |

> **✅ keep-alive の扱い**
> - HTTP/1.1 はデフォルト接続維持。送信完了後、Connection: close が無ければ READING に戻り次のリクエストを待つ。
> - 最初は『毎回 close する単純な 1.1』で実装を通し、安定後に keep-alive を有効化するのが安全 (第9章の順序)。

---

## 9. 実装順序と分担

「同期で済むものを先に完成させ、待つもの (CGI) を後で非同期に足す」のが鉄則。最初から全部を非同期で作ると複雑になりすぎる。

### 9.1 担当割り当て

| 担当 | 範囲 |
|---|---|
| **担当 A — 基盤側** | ConfigParser / Config、EventLoop、ListenSocket、Connection、ノンブロッキング送受信、複数ポート、タイムアウト掃除、シグナル処理、Makefile、CGI パイプの poll 統合 (I/O 側) |
| **担当 B — アプリ側** | HttpRequest 逐次パーサ、Router、RequestHandler、HttpResponse、静的配信・autoindex・アップロード・DELETE・リダイレクト、エラーページ、CgiProcess (CGI 側) |

### 9.2 マイルストーン

| 週 | 担当 A | 担当 B |
|---|---|---|
| 1 | 単一 poll() の echo サーバー (完了済み)。設定パーサの骨格に着手 | HttpRequest パーサ (GET の行・ヘッダ) を単体テストで実装 |
| 2 | Connection 状態機械、複数ポート listen、設定パーサ完成 | 静的ファイル配信、404/403、Router::match (NULL 処理)、パストラバーサル防止、HttpResponse |
| 3 | タイムアウト/切断処理、送信バッファ完成、keep-alive 無効版で結合 | POST アップロード、DELETE、autoindex、エラーページ、リダイレクト、405+Allow |
| 4 | CGI パイプの poll 統合 (attachCgi / onCgiDone)、CGI タイムアウト/kill を B と協調 | CgiProcess (fork+execve+環境変数)、chunked un-chunk、504 |
| 5 | keep-alive 有効化、ストレステスト、リーク/fd 枯渇調査 | ブラウザ互換・NGINX 比較、デモ用 .conf と各機能の確認ファイル |

> **✅ 結合のコツ**
> - echo サーバーの handleRead の『受信をそのまま echo』を『HttpRequest に供給して handle を呼ぶ』に差し替えるのが結合の第一歩。
> - 第7章の境界面 (handle / onCgiDone / attachCgi / findServer) を着手前に確定しておけば、結合は差し替えだけで済む。
> - CGI は最後。静的が完全に動いてから着手する。

---

## 10. ファイル構成とクラス一覧

### 10.1 ディレクトリ構成

層構成をそのままフォルダに対応させ、フォルダ単位で担当を分ける。同じファイルを2人で編集する状況を避け、Git のコンフリクトを抑える。新しいクラスを足すときも置き場所が一意に決まる。

```
webserv/
├── Makefile                # 担当A (NAME/all/clean/fclean/re)
├── README.md               # 両者 (提出前に作成)
├── webserv.hpp             # 両者 (共有インターフェイス = 境界面の契約)
├── config/                 # デモ用 .conf 一式
│   └── default.conf
├── www/                    # 静的サイト・アップロード保存先・エラーページ
│   ├── site/
│   ├── uploads/
│   └── errors/
├── cgi-bin/                # テスト用 CGI スクリプト (.py など)
└── src/
    ├── main.cpp            # 担当A (起動・設定読み込み・EventLoop 起動)
    ├── config/             # 担当A — 設定層
    │   ├── Config.cpp
    │   └── ConfigParser.cpp
    ├── net/                # 担当A — I/O層 (基盤コア)
    │   ├── EventLoop.cpp
    │   ├── ListenSocket.cpp
    │   └── Connection.cpp
    ├── http/               # 担当B — HTTP層
    │   ├── HttpRequest.cpp
    │   ├── HttpResponse.cpp
    │   ├── Router.cpp
    │   └── RequestHandler.cpp
    └── cgi/                # 担当B (poll 統合は A と協調) — CGI層
        └── CgiProcess.cpp
```

> **ℹ️ フォルダと担当の対応**
> - src/config と src/net が担当A の領域。とくに net/ の3クラス (EventLoop / ListenSocket / Connection) が基盤コアで、echo サーバーを整理して最初に作り切る部分 (第9章フェーズ1)。
> - src/http と src/cgi が担当B の領域。CgiProcess のパイプを poll に載せる部分だけ A と接点を持つ (第6・7章)。
> - webserv.hpp と Makefile、config/・www/・cgi-bin/ のデモ資材は両者で共有。後半はチケット制で手の空いた人が拡充する。

### 10.2 全クラス一覧

実装するクラスの全体像。各クラスの公開インターフェイスの正確な形は webserv.hpp を正とし、本表は責務と配置・担当の地図として使う。

| クラス | 責務 (1行) | 配置 | 担当 |
|---|---|---|---|
| Config | 複数 ServerConfig を保持し host:port から引く | src/config | A |
| ConfigParser | .conf をパースし Config を構築・検証する | src/config | A |
| EventLoop | 唯一の poll() を持ち全 fd のイベントを振り分ける | src/net | A |
| ListenSocket | 1 つの host:port を待ち受け accept する | src/net | A |
| Connection | 1 接続の状態機械、ノンブロッキング送受信 | src/net | A |
| HttpRequest | 受信バイトを逐次パースする (chunked 含む) | src/http | B |
| HttpResponse | ステータス/ヘッダ/ボディをバイト列にする | src/http | B |
| Router | パスから最長一致の location を選ぶ | src/http | B |
| RequestHandler | メソッド別処理、静的配信/アップロード/削除/CGI 振り分け | src/http | B |
| CgiProcess | CGI を fork+execve し、パイプ I/O で結果を得る | src/cgi | B (+A) |

構造体 (LocationConfig / ServerConfig) は Config と同じ設定層に属し、webserv.hpp で定義する。enum (Method / ParseState / HandleResult / CgiState / ConnState) も webserv.hpp に集約する。

> **✅ この表と webserv.hpp の関係**
> - 本表はクラスの『存在・責務・配置・担当』という安定した情報のみを持つ。メンバ変数や private メソッドの細部は実装で変わるため、ここには書かず webserv.hpp とコードに任せる。
> - クラスを新設・改名したらこの表と webserv.hpp の両方を更新する。

---

## 11. テスト方針と提出物

### 11.1 テスト

- ブラウザ (Chrome/Firefox) で静的サイト・アップロード・CGI が動くことを確認する。
- telnet/nc で生の HTTP を送り、不正・部分的リクエストでも落ちないか確認する。
- Python などでスクリプト化した自動テストを書く (1 つのツールだけに頼らない)。
- NGINX と同条件で叩き、ヘッダや挙動の差分を比較する (HTTP バージョン差に注意)。
- 並列接続・大ボディ・遅い送信などのストレステストで可用性を確認する (クラッシュは 0 点)。
- 長時間稼働で fd リーク (close 漏れ) やメモリリークが無いか監視する。

### 11.2 提出物として用意するもの

- Makefile (NAME, all, clean, fclean, re。不要な再リンクをしない)。
- ソース (*.hpp / *.cpp / *.tpp / *.ipp) と、デモ用の設定ファイル・テスト用ファイル一式。
- 各機能 (静的・autoindex・アップロード・DELETE・CGI・エラーページ・複数ポート・リダイレクト) を評価時に実演できる .conf とコンテンツ。
- README.md (第11.3章)。

### 11.3 README に必須の項目

- 1 行目を斜体で: *This project has been created as part of the 42 curriculum by &lt;login1&gt;, &lt;login2&gt;.*
- Description: プロジェクトの目的と概要。
- Instructions: コンパイル・実行方法。
- Resources: 参考資料と、AI をどのタスク・どの部分で使ったかの説明。
- 英語で記述すること。

> **ℹ️ 評価での心構え**
> - 分担しても『自分が書いていない部分も説明できる』ことが問われる。週に数回はコードを読み合う。
> - 特に CGI とノンブロッキングの相互作用 (第6章) は 2 人で必ず詰めること。
> - AI 生成コードは理解して責任を持てる範囲でのみ使い、peer review を必ず通す。
