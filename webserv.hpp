/* ************************************************************************** */
/*                                                                            */
/*   webserv.hpp  —  チーム共有インターフェイス (境界面の契約)                */
/*                                                                            */
/*   このファイルの目的:                                                      */
/*     担当A (基盤/I/O層) と担当B (アプリ/HTTP層) が、お互いの「中身」を      */
/*     知らなくても並行して開発できるように、2つのパーツが呼び合う関数の      */
/*     「形」(引数と戻り値) だけを先に決めたもの。                            */
/*                                                                            */
/*   使い方:                                                                  */
/*     1. 着手前に2人でこのファイルを一緒に読み、形に合意する。              */
/*     2. 合意したら、各自このヘッダに沿って自分の担当クラスを実装する。      */
/*     3. ここの「中身 (実装)」はまだ書かない。形 (宣言) だけを決める。       */
/*                                                                            */
/*   重要:                                                                    */
/*     これは「たたき台」であり、実装を進める中で必ず調整が入る。            */
/*     特に引数の細部・戻り値・メンバの持ち方は変わりうる。変更するときは    */
/*     必ず相方に共有し、双方が同じこのファイルを更新すること。              */
/*     (設計書 第7章「モジュール間の境界面」に対応)                          */
/*                                                                            */
/* ************************************************************************** */

#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <ctime>
#include <sys/types.h>   // pid_t

/* ========================================================================== */
/*  前方宣言                                                                  */
/*  互いを指すだけのクラスは、ここで「存在する」とだけ宣言しておく。          */
/*  これにより、実体の定義順に縛られずポインタ/参照で参照し合える。           */
/* ========================================================================== */

class Connection;       // I/O層: 1クライアント接続
class CgiProcess;       // CGI層: 1つのCGI実行
class EventLoop;        // I/O層: 単一poll()のループ


/* ========================================================================== */
/*  共通の列挙型                                                              */
/* ========================================================================== */

// HTTPメソッド。Mandatoryで必須なのは GET / POST / DELETE の3つ。
// それ以外が来たら METHOD_UNKNOWN とし、501 Not Implemented で返す。
enum Method
{
    METHOD_GET,
    METHOD_POST,
    METHOD_DELETE,
    METHOD_UNKNOWN
};


/* ========================================================================== */
/*  設定層 (担当A)                                                            */
/*                                                                            */
/*  .conf をパースして作る不変データ。起動時に確定し、以後変更しない。        */
/*  I/O層・HTTP層の両方が「読むだけ」で参照する。                             */
/*  --- 境界面の決めごと #4: 設定の引き方 (findServer / Router::match) ---     */
/* ========================================================================== */

// URL/route ごとの設定。
struct LocationConfig
{
    std::string                         path;            // 例: "/", "/upload", "/cgi-bin"
    std::set<std::string>               allowedMethods;  // 許可メソッド (例: {"GET","POST"})
    std::string                         root;            // パスのマッピング先ディレクトリ
    std::string                         index;           // ディレクトリ要求時の既定ファイル
    bool                                autoindex;       // ディレクトリ一覧の on/off
    std::pair<int, std::string>         redirect;        // return: <コード, URL> (無ければ first==0)
    std::string                         uploadStore;     // アップロード保存先 (空なら不可)
    std::map<std::string, std::string>  cgi;             // 拡張子 -> インタプリタのパス
};

// 1つの server ブロック。1つの host:port に対応する。
struct ServerConfig
{
    std::string                         host;            // 例: "0.0.0.0"
    int                                 port;            // 例: 8080
    std::map<int, std::string>          errorPages;      // ステータス -> エラーページのパス
    size_t                              clientMaxBodySize;
    std::vector<LocationConfig>         locations;
};

// 設定全体。複数の server を保持する。
class Config
{
    std::vector<ServerConfig> _servers;

public:
    const std::vector<ServerConfig>& servers() const;

    // host:port から担当の ServerConfig を引く (複数ポート対応の要)。
    // 見つからなければ NULL。
    // [呼ぶ人] I/O層 (新しい接続がどの server に属するか決めるとき)
    const ServerConfig* findServer(const std::string& host, int port) const;
};


/* ========================================================================== */
/*  HTTPリクエスト (担当B)                                                     */
/*                                                                            */
/*  受信バイト列を「少しずつ」受け取り、途中状態でも壊れずにパースを進める。  */
/*  ノンブロッキングの肝。consume() を繰り返し呼べば最終的に完成する。        */
/* ========================================================================== */

enum ParseState
{
    PARSE_REQUEST_LINE,   // メソッド・ターゲット・バージョン行
    PARSE_HEADERS,        // ヘッダ群
    PARSE_BODY,           // Content-Length ベースの本文
    PARSE_CHUNKED,        // Transfer-Encoding: chunked の本文
    PARSE_COMPLETE,       // 完成
    PARSE_ERROR           // 不正リクエスト (400 など)
};

class HttpRequest
{
    Method                              _method;
    std::string                         _target;   // 生のリクエストターゲット
    std::string                         _path;     // デコード済みパス
    std::string                         _query;    // クエリ文字列 (CGI用)
    std::map<std::string, std::string>  _headers;
    std::string                         _body;     // un-chunk 済みの本文
    ParseState                          _state;

public:
    // 受け取ったバイトを供給し、パース状態を進める。
    // [呼ぶ人] I/O層 (Connection が recv するたびに)
    void consume(const std::string& bytes);

    bool isComplete() const;   // PARSE_COMPLETE か
    bool hasError() const;     // PARSE_ERROR か

    // アクセサ (HTTP層が処理に使う)
    Method                 method() const;
    const std::string&     path() const;
    const std::string&     query() const;
    const std::string&     body() const;
    std::string            header(const std::string& name) const; // 無ければ空文字

    void reset();   // keep-alive で次のリクエストを受けるとき初期化
};


/* ========================================================================== */
/*  HTTPレスポンス (担当B)                                                     */
/*                                                                            */
/*  ステータス・ヘッダ・ボディを組み立て、送信用バイト列にする。              */
/* ========================================================================== */

class HttpResponse
{
    int                                 _status;   // 200, 404 ...
    std::map<std::string, std::string>  _headers;
    std::string                         _body;

public:
    void setStatus(int code);
    void setHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);   // Content-Length も自動設定

    // 送信用のバイト列にする。
    // [呼ぶ人] I/O層 (Connection が送信バッファに詰めるとき)
    std::string serialize() const;
};


/* ========================================================================== */
/*  ルーティング (担当B)                                                       */
/*                                                                            */
/*  --- 境界面の決めごと #4: 設定の引き方 ---                                  */
/* ========================================================================== */

class Router
{
public:
    // パスから最長一致する location を選ぶ。正規表現は不要。
    // 一致する location が無ければ NULL (呼び出し側が 404 を返す)。
    // [呼ぶ人] I/O層 (Connection がリクエスト完成時に)
    static const LocationConfig* match(const ServerConfig& srv,
                                       const std::string& path);
};


/* ========================================================================== */
/*  リクエストハンドラ (担当B)                                                 */
/*                                                                            */
/*  --- 境界面の決めごと #1: handle() の形 (最重要) ---                        */
/*                                                                            */
/*  メソッドごとの処理の中心。                                                */
/*    静的処理 (GET/DELETE/アップロード/エラー) はその場で完成し、            */
/*      RESPONSE_READY を返して out にレスポンスを入れる。                     */
/*    CGI 対象なら CgiProcess を起動し、CGI_STARTED を返す。                   */
/*      結果は後から poll 経由で届く (out には何も入れない)。                  */
/* ========================================================================== */

// handle() が同期完了したか、CGIに切り替わったかを呼び出し側へ伝える。
enum HandleResult
{
    RESPONSE_READY,   // out にレスポンスが入った (同期完了)
    CGI_STARTED       // CGIを起動した。結果は後から onCgiDone で届く
};

class RequestHandler
{
public:
    // [呼ぶ人] I/O層 (Connection がリクエスト完成時に)
    // [引数]
    //   req   : 完成したリクエスト
    //   loc   : Router::match で選ばれた location (呼び出し側でNULLチェック済み)
    //   srv   : この接続の属する server (エラーページ等に使う)
    //   conn  : 依頼元の接続。CGI起動時に「どの接続のためのCGIか」を結ぶのに使う
    //   out   : RESPONSE_READY のとき、ここに結果を書き込む
    // [戻り値] RESPONSE_READY か CGI_STARTED
    HandleResult handle(const HttpRequest& req,
                        const LocationConfig& loc,
                        const ServerConfig& srv,
                        Connection& conn,
                        HttpResponse& out);

    // 任意のステータスのエラーレスポンスを作る (設定にあればそのページ、無ければ内蔵)。
    // [呼ぶ人] I/O層・HTTP層の両方 (404 などを返したいとき)
    HttpResponse makeError(int code, const ServerConfig& srv);

private:
    // メソッド別の内部処理 (担当Bが自由に実装してよい部分)
    HandleResult doGet(const HttpRequest& req, const LocationConfig& loc,
                       const ServerConfig& srv, Connection& conn, HttpResponse& out);
    HandleResult doPost(const HttpRequest& req, const LocationConfig& loc,
                        const ServerConfig& srv, Connection& conn, HttpResponse& out);
    HandleResult doDelete(const HttpRequest& req, const LocationConfig& loc,
                          const ServerConfig& srv, HttpResponse& out);

    HttpResponse makeRedirect(const LocationConfig& loc);

    // パストラバーサル対策込みのパス解決。root の外に出るパスは false を返す。
    // (解決した実体パスが resolved に入る)
    bool resolvePath(const LocationConfig& loc, const std::string& urlPath,
                     std::string& resolved);
};


/* ========================================================================== */
/*  CGIプロセス (担当B が中身、担当A と接点を持つ)                             */
/*                                                                            */
/*  CGIはパイプという「待たされるfd」を扱う唯一の非同期処理。                  */
/*  パイプを poll に載せ、入力を書き・出力を読み、完了したら接続へ返す。       */
/* ========================================================================== */

enum CgiState
{
    CGI_WRITING_INPUT,   // POSTボディを CGI の stdin に書いている
    CGI_READING_OUTPUT,  // CGI の stdout から結果を読んでいる
    CGI_DONE,            // 完了 -> レスポンス組み立てへ
    CGI_FAILED           // 異常 -> 500
};

class CgiProcess
{
    pid_t        _pid;
    int          _inFd;        // CGI の stdin へ書く側 (POSTボディ)
    int          _outFd;       // CGI の stdout から読む側
    std::string  _inBuf;       // 未送信のリクエストボディ
    std::string  _outBuf;      // CGI からの出力蓄積
    CgiState     _state;
    Connection*  _owner;       // どのクライアント接続のためのCGIか (onCgiDoneの宛先)
    time_t       _startTime;   // 起動時刻 (タイムアウト判定用)

public:
    // fork + execve でCGIを起動し、パイプを用意する。
    // [呼ぶ人] HTTP層 (RequestHandler が CGI 対象と判定したとき)
    bool start(const HttpRequest& req, const LocationConfig& loc, Connection& owner);

    // パイプの readiness 通知を受けて少しずつI/Oする。
    // [呼ぶ人] I/O層 (EventLoop が CGI パイプの POLLIN/POLLOUT を検知したとき)
    void onWritable();   // _inBuf を stdin パイプへ書く。書き切ったら close (EOF)
    void onReadable();   // stdout パイプから _outBuf へ読む。EOF で CGI_DONE

    // タイムアウト関連 (ハング防止)。
    // [呼ぶ人] I/O層 (メインループの定期チェック)
    bool isTimedOut(time_t now) const;
    void killProcess();  // SIGKILL で強制終了し waitpid で回収

    int  inFd() const;
    int  outFd() const;
    CgiState state() const;

    // CGI出力を HttpResponse に変換する。
    HttpResponse buildResponse() const;
};


/* ========================================================================== */
/*  接続 (担当A)                                                              */
/*                                                                            */
/*  1クライアント接続の状態機械。recv/send を小刻みに回し、リクエスト完成時に */
/*  HTTP層へ処理を委ね、CGI完了時に送信を始める。                             */
/*                                                                            */
/*  --- 境界面の決めごと #3: onCgiDone() の形 ---                              */
/* ========================================================================== */

enum ConnState
{
    READING,        // リクエスト受信中
    HANDLING,       // 同期処理中 (すぐ WRITING へ。実装上は省略可)
    CGI_RUNNING,    // CGIの結果を待っている (非同期)
    WRITING,        // レスポンス送信中
    CLOSING         // 切断処理中
};

class Connection
{
    int                 _fd;
    ConnState           _state;
    std::string         _inBuf;        // 受信した生バイト
    std::string         _outBuf;       // 送信待ちバイト
    HttpRequest         _req;
    const ServerConfig* _server;       // この接続を受けた listen に対応
    time_t              _lastActive;

public:
    // poll の readiness 通知を受けて動く。
    // [呼ぶ人] I/O層 (EventLoop がこの接続の POLLIN/POLLOUT を検知したとき)
    void onReadable();   // recv -> _req.consume() -> 完成なら handle() を呼ぶ
    void onWritable();   // _outBuf を send。送り切ったら keep-alive 判定

    // EventLoop が毎ループ「今どの poll イベントを監視してほしいか」を聞く。
    // 状態ベースで返す (WRITING 中は POLLOUT、それ以外は POLLIN)。
    // これが「問い合わせ型」pollfd 管理の要 (EventLoop 側の説明を参照)。
    // [呼ぶ人] I/O層 (EventLoop::buildPollFds が pollfd を組むとき)
    short wantedEvents() const;

    // この接続を閉じるべきか (状態が CLOSING か)。
    // [呼ぶ人] I/O層 (dispatch がイベント処理後、走査を終えてからまとめて閉じる)
    bool shouldClose() const;

    // CGI完了時にHTTP/CGI層から呼ばれ、送信フェーズに入る。
    // [呼ぶ人] CGI層 (CgiProcess が CGI_DONE になったとき)
    void onCgiDone(const HttpResponse& response);

    // タイムアウト判定 (ハング防止)。
    // [呼ぶ人] I/O層 (メインループの定期チェック)
    bool isTimedOut(time_t now) const;

    int       fd() const;
    ConnState state() const;
};


/* ========================================================================== */
/*  待ち受けソケット (担当A)                                                   */
/*                                                                            */
/*  1つの host:port に対応。読めるイベントで accept() し、新しい接続を作る。   */
/*  設定の listen の数だけ生成する (複数ポート対応)。                          */
/* ========================================================================== */

class ListenSocket
{
    int                 _fd;
    const ServerConfig* _server;   // この listen に対応する設定

public:
    // socket -> setsockopt -> fcntl(O_NONBLOCK) -> bind -> listen。
    bool setup(const ServerConfig& srv);

    // accept() して新しい client fd を作り、ノンブロッキング化する。
    // [呼ぶ人] I/O層 (EventLoop が listen fd の POLLIN を検知したとき)
    // [戻り値] 新しい client fd (これ以上接続が無ければ -1)
    int acceptClient();

    int                 fd() const;
    const ServerConfig* server() const;
};


/* ========================================================================== */
/*  イベントループ (担当A)                                                     */
/*                                                                            */
/*  プログラム全体で唯一の poll() を持つ中心。全fdを監視し、イベントを        */
/*  ListenSocket / Connection / CgiProcess へ振り分ける。                     */
/*                                                                            */
/*  --- 境界面の決めごと #2: attachCgi() の形 ---                              */
/* ========================================================================== */

class EventLoop
{
    std::vector<struct pollfd>      _pfds;
    std::map<int, ListenSocket*>    _listeners;
    std::map<int, Connection*>      _conns;
    std::map<int, CgiProcess*>      _cgis;     // CGIパイプfd -> CgiProcess

public:
    void addListener(ListenSocket* ls);

    // pollfd 配列は「問い合わせ型」: 毎ループ buildPollFds() で作り直し、
    // 各 Connection の wantedEvents() を聞いて監視イベントを決める。
    // よって registerFd / modifyEvents / unregisterFd のような明示的な
    // 登録・変更 API は持たない。「POLLOUT は送信待ちのときだけ」という不変条件は
    // Connection の状態から自動導出されるので、呼び忘れで壊れることがない。
    // (Connection → EventLoop の一方向依存。Connection は loop を呼び返さない)

    // CGIのパイプを poll の監視対象に載せる/外す。
    // [呼ぶ人] CGI層 (CgiProcess::start の直後に attach、完了/kill時に detach)
    void attachCgi(int pipeFd, CgiProcess* p);
    void detachCgi(int pipeFd);

    // メインループ: poll() で待ち、イベントを振り分け、タイムアウトを掃除する。
    void run();
};


/* ========================================================================== */
/*  境界面の要約 (4つの決めごと)                                               */
/*                                                                            */
/*   #1 基盤 → アプリ : RequestHandler::handle(req, loc, srv, conn, out)       */
/*                      Connection がリクエスト完成時に呼ぶ。                  */
/*                                                                            */
/*   #2 CGI → 基盤    : EventLoop::attachCgi(pipeFd, cgiProcess)              */
/*                      CgiProcess::start の直後に呼び、パイプを poll に載せる。*/
/*                                                                            */
/*   #3 CGI → 基盤    : Connection::onCgiDone(response)                       */
/*                      CGI 完了時に呼び、接続を送信フェーズへ移す。           */
/*                                                                            */
/*   #4 設定の引き方  : Config::findServer(host, port)                        */
/*                      Router::match(srv, path)                              */
/*                      両層が同じ方法で設定を読む。                          */
/*                                                                            */
/*   この4点が揃えば、担当Aと担当Bは互いの中身を知らずに並行開発でき、        */
/*   後で差し替えるだけで結合できる。                                         */
/*                                                                            */
/*   再掲: これはたたき台。実装で調整が入ったら必ず相方に共有し、             */
/*   双方が同じこのファイルを更新すること。                                   */
/* ========================================================================== */

#endif // WEBSERV_HPP
