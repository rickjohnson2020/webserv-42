# Webserv 完全テキストブック

**— この一冊で、概念の理解から実装・評価対策まで —**

C++98 / poll() / HTTP/1.1 / Mac環境 / 2人チーム対応

---

## この本の使い方

本書は 42 School の Webserv 課題を「理解して」完走するためのテキストブックである。読者として、Web のサーバー/クライアントの一般知識はあるが、C++ でのシステムプログラミング経験はない人を想定している。

本書は5部構成になっている。

- **第I部 基礎知識編** — fd、ソケット、ノンブロッキング、poll。実装の土台になる OS の概念。ここが一番大事。
- **第II部 HTTP編** — HTTPメッセージの生の姿と、HTTP/1.1 で実装すべき範囲。
- **第III部 設計編** — 4層アーキテクチャ、状態機械、チーム開発の境界面。
- **第IV部 実装編** — 各コンポーネントの実装ガイド。落とし穴と対策込み。
- **第V部 仕上げ編** — テスト、評価(defense)対策、1ヶ月ロードマップ。

読み方の推奨: **第I部を飛ばさないこと**。実装で詰まる原因の9割は第I部の概念の未消化にある。逆に第I部が腑に落ちていれば、第IV部の実装は「概念をコードに写す作業」になる。

各章の最後に「✅ この章の要点」を置いた。復習と、相方との認識合わせに使ってほしい。

---
---

# 第I部 基礎知識編

---

## 第1章 Webservとは何か — 課題の全体像

### 1.1 何を作るのか

一言でいうと、**「NGINXのミニチュア版」を C++98 で自作する**課題である。

```
                  HTTPリクエスト
   ┌─────────┐   ─────────────────▶   ┌──────────────┐
   │ ブラウザ │                        │  webserv     │
   │ (Chrome  │   ◀─────────────────   │  (あなたが    │
   │  など)   │    HTTPレスポンス       │   作るもの)   │
   └─────────┘                        └──────────────┘
                                        │
                                        ├─ 静的ファイルを返す (GET)
                                        ├─ アップロードを受ける (POST)
                                        ├─ ファイルを消す (DELETE)
                                        └─ CGIスクリプトを実行する
```

起動はこうする。

```
./webserv [configuration file]
```

設定ファイル(NGINX の server ブロック風)で「どのポートで待つか」「どの URL がどのフォルダに対応するか」などを指定し、実際のブラウザからアクセスして動くサーバーを作る。

### 1.2 何を作らないのか

混同しやすいので明確にしておく。

- **クライアントサイドのUI は C++ では作らない。** ブラウザに表示されるページは、ただの HTML/CSS/JS ファイルで、デモ用に手書きで用意して `www/` に置く。サーバーはそれを「読んで返すだけ」。
- **HTTPライブラリは使えない。** 外部ライブラリ・Boost は全面禁止。HTTP のパースもレスポンス生成も、すべて自分で文字列処理として書く。
- **既製サーバーへの丸投げは禁止。** NGINX を `execve` で起動して仕事を肩代わりさせるのは不可。

### 1.3 絶対に守るルール(破ると採点 0)

この課題には「破った瞬間 0 点」のルールがいくつかある。**本書全体を通して繰り返し出てくる**ので、まず一覧で頭に入れてほしい。

| # | ルール | 詳細章 |
|---|---|---|
| 1 | poll() の readiness 通知を受ける**前に**、ソケット/パイプへ read・recv・write・send してはならない | 第5・6章 |
| 2 | read/write の**後に** errno を見てサーバーの挙動を変えてはならない | 第6章 |
| 3 | プログラムは**いかなる状況でもクラッシュしてはならない**(メモリ不足でも) | 第19章 |
| 4 | fork は **CGI 以外の用途で使用禁止** | 第18章 |
| 5 | 別の Web サーバーを execve してはならない | — |

補足: **通常のディスクファイルはルール1の例外**である(第6章で詳述)。

### 1.4 技術的な前提(本書の方針)

本書は以下の技術選択を前提とする(理由は各章で説明する)。

- **I/O多重化は poll() を使う。** select/epoll/kqueue も課題上は許可されているが、poll が Mac・Linux 両対応で最も無難(第5章)。
- **HTTP/1.1 を目標にする。** 課題は HTTP/1.0 を「参照点」とするが、実ブラウザ互換と chunked 要件を考えると 1.1 が現実的(第9章)。
- **Mac 環境で開発する。** epoll は Linux 専用で Mac では使えない。fcntl の許可フラグは F_SETFL / O_NONBLOCK / FD_CLOEXEC のみ(第4章)。
- **Bonus(cookie/session、複数CGI)は対象外。**

### 1.5 コンパイル条件

```
c++ -Wall -Wextra -Werror -std=c++98
```

- 警告はすべてエラー扱い。ひとつでも警告があるとコンパイルが通らない。
- **C++98 標準**に準拠。モダン C++(auto、範囲for、nullptr、スマートポインタ等)は使えない(第7章)。
- Makefile 必須(NAME / all / clean / fclean / re。不要な再リンク禁止)。

### ✅ この章の要点

- 作るのは「HTTPサーバーのエンジン」。UI は HTML ファイルとして別途用意するだけ。
- 「readiness 前の read/write」「read/write 後の errno」「クラッシュ」「CGI 以外の fork」は即 0 点。
- poll() + HTTP/1.1 + C++98 が本書の前提。

---

## 第2章 ファイルディスクリプタ(fd) — すべての土台

Webserv の実装は、突き詰めれば「たくさんの fd を正しく管理すること」に尽きる。まずこの概念を完全に理解しよう。

### 2.1 fd とは「OSが発行する整理番号」

クローク(荷物預かり所)を想像してほしい。荷物を預けると番号札を渡される。以後、あなたは**番号札を見せるだけ**で荷物を出し入れできる。荷物が奥でどう保管されているかは知らなくていい。

fd はこの番号札である。プログラムが何か(ファイル、ソケット、パイプ)を「開く」と、OS が内部で実体を用意し、引き換えに**小さな整数**を返す。以後、プログラムはその番号で読み書きを指示する。

```
   あなたのプログラム              OS(カーネル)
   ┌──────────────┐            ┌─────────────────────┐
   │              │  "3番に書く" │  fd表                │
   │  write(3,..) │──────────▶ │   0: 標準入力         │
   │              │            │   1: 標準出力         │
   │              │            │   2: 標準エラー出力    │
   │              │            │   3: ソケット(実体)◀──┼── ネットワークへ
   │              │            │   4: 開いたファイル    │
   └──────────────┘            └─────────────────────┘
```

- 0, 1, 2 は最初から予約済み(標準入力/出力/エラー)。
- 新しく開くと、たいてい 3 から順に割り当てられる。

### 2.2 なぜ「ファイル」なのにソケットも fd なのか

Unix 系 OS の根本思想に「**ほとんどの入出力は"ファイルのように"読み書きできるものとして統一的に扱う**」というものがある。ディスク上のファイルも、ネットワークのソケットも、プロセス間のパイプも、「データが流れる通り道」という点では同じ。だから全部まとめて fd で表し、同じ read/write(ソケットなら recv/send)で扱えるようにしてある。

Webserv で扱う fd はこの4種類である。

| fd の種類 | 何の通り道か | 誰が作るか |
|---|---|---|
| listen ソケット | 「このポートへの新規接続」の入口 | socket() |
| クライアントソケット | ブラウザ1つとの通信路 | accept() |
| CGI のパイプ | CGI プロセスとのデータの通り道 | pipe() |
| ディスクファイル | 要求された HTML や画像 | open() |

### 2.3 fd は「借りたら返す」リソース

**ここが Web 系言語の感覚と最も違うところ。** ガベージコレクタのある言語では使い終わったリソースは勝手に片付くが、C++ では自分で管理する。

fd を得たら(socket / accept / open / pipe)、使い終わったら必ず `close(fd)` で OS に返却する。返し忘れると:

```
接続が来るたび fd を発行 → close しない → fd がどんどん溜まる
→ やがて OS の上限に達する → 「もう新しい fd を発行できません」
→ accept が失敗し、新規接続を一切受けられなくなる(fd枯渇)
```

長時間動き続けるサーバーでは、この **fd リーク**が致命傷になる。「クライアントが切断したら close」「CGI が終わったらパイプを close」「ファイルを読み終わったら close」——本書の実装編では、fd を得る場所と返す場所を常にペアで意識する。

### 2.4 「待たされる fd」と「待たされない fd」

fd には性質の違いがあり、これが課題の重要ルールに直結する。

- **待たされる fd(ソケット、パイプ)**: 相手(ブラウザ、CGIプロセス)の都合でデータが来る。いつ読めるようになるか、こちらはコントロールできない。→ **poll() の管理下に置く義務がある**(第5章)。
- **待たされない fd(通常のディスクファイル)**: 中身はもうディスク上に全部ある。read すればすぐ読める。→ **poll() を通さず直接 read/write してよい**(課題が明示する例外)。

判断基準はひとつ: 「その fd は、**相手の都合**でデータを待つことがあるか?」

### ✅ この章の要点

- fd = OS が発行する入出力の整理番号(小さな整数)。ファイルもソケットもパイプも同じ仕組み。
- fd は借りたら必ず close で返す。返し忘れ(fdリーク)はサーバーの死につながる。
- ソケット・パイプは「待たされる fd」で poll 必須。ディスクファイルは例外で直接読み書きしてよい。

---

## 第3章 ソケットプログラミング入門

サーバーがネットワークで通信する仕組みを、システムコールのレベルで理解する。この章の内容は担当A(基盤)の中核であり、echo サーバー(第15章)の土台である。

### 3.1 サーバーが接続を受けるまでの儀式

サーバー側は、決まった順番のシステムコールで「店を開ける」。

```
 socket()      「電話機を1台もらう」(fd が返る)
    │
 setsockopt()  「再起動してもすぐ同じ番号を使えるように」(SO_REUSEADDR)
    │
 fcntl()       「この電話機を"待たない"モードに」(O_NONBLOCK; 第4章)
    │
 bind()        「電話番号(IP:ポート)をこの電話機に割り当てる」
    │
 listen()      「着信を受け付け開始。保留列も用意」
    │
 (poll で着信を待つ)
    │
 accept()      「かかってきた1本の通話を取る」
                → 新しい fd が返る(この通話専用の子機)
```

**最重要ポイント: accept() は「新しい fd」を返す。** listen 用の fd は「着信を待つ親機」であり続け、accept のたびに「その通話専用の子機」(クライアント fd)が生まれる。クライアントが10人つながれば、親機1 + 子機10 で fd が11個になる。

```
              listen fd (親機; ポート8080で待つ)
                     │ accept
        ┌────────────┼────────────┐
        ▼            ▼            ▼
   client fd 4   client fd 5   client fd 6
   (ブラウザA)    (ブラウザB)    (ブラウザC)
```

### 3.2 各システムコールの最小知識

実装で書く形をそのまま示す(エラーチェックは省略。実際は毎回戻り値を確認する)。

```cpp
#include <sys/socket.h>
#include <netinet/in.h>

// (1) ソケット作成: IPv4 (AF_INET), TCP (SOCK_STREAM)
int fd = socket(AF_INET, SOCK_STREAM, 0);

// (2) 再起動時の "Address already in use" を防ぐお約束
int opt = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

// (3) アドレスを組み立てて bind
struct sockaddr_in addr;
std::memset(&addr, 0, sizeof(addr));
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 全インターフェイスで待つ
addr.sin_port        = htons(8080);        // ポート番号
bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

// (4) 待ち受け開始
listen(fd, SOMAXCONN);

// (5) (pollで読めると分かったら) 接続を受ける
int clientFd = accept(fd, NULL, NULL);
```

### 3.3 htons / htonl — バイトオーダーの罠

`htons(8080)` の `htons` は "host to network short" の略で、**バイトの並び順を変換する**関数である。

数値をメモリに置くとき、CPU によって「大きい桁から置く(ビッグエンディアン)」「小さい桁から置く(リトルエンディアン)」の流儀が違う。ネットワーク上は**ビッグエンディアンに統一**という世界共通ルールがあるため、ポート番号や IP アドレスをソケット API に渡すときは必ず変換を挟む。

| 関数 | 意味 | 使う場面 |
|---|---|---|
| htons | host→network (short: 16bit) | ポート番号 |
| htonl | host→network (long: 32bit) | IPアドレス |
| ntohs / ntohl | network→host(逆変換) | 受信した値を読むとき |

「ポートに `htons` を付け忘れて、全然違うポートで待っていた」は定番のバグ。**sin_port と sin_addr には必ず htons/htonl** と覚える。

### 3.4 データの送受信: recv と send

接続が確立したら、クライアント fd に対して読み書きする。

```cpp
char buf[4096];
ssize_t n = recv(clientFd, buf, sizeof(buf), 0);   // 読む
ssize_t m = send(clientFd, data, len, 0);           // 書く
```

**戻り値の意味を体に叩き込むこと。** これが後の第6章(errno 禁止ルール)の伏線になる。

| 戻り値 | recv の意味 | send の意味 |
|---|---|---|
| 正の値 n | n バイト読めた | n バイト送れた(**全部とは限らない!**) |
| 0 | **相手が接続を閉じた** | (通常発生しない) |
| -1 | 今は読めない/エラー | 今は送れない/エラー |

2つの重要な現実:

1. **recv は「あるだけ」しか読まない。** リクエスト全体が一度に届く保証はなく、途中までしか来ていないこともある。→ だから「逐次パース」が必要(第16章)。
2. **send は「送れるだけ」しか送らない。** 大きいレスポンスは一度で送り切れない。→ だから「送信バッファに残りを溜めて、次に書けるときに続きを送る」実装が必要(第15章)。

### 3.5 システムコールは「失敗しうる関数」

Web 系の感覚だと「関数は普通成功する」が、システムプログラミングでは**あらゆるシステムコールが失敗しうる**。socket も bind も listen も、失敗すると -1 を返す。

**毎回、戻り値をチェックする**のが鉄則である。

```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);
if (fd < 0) {
    // 失敗。ログを出して適切に処理(起動時なら終了してよい)
}
```

注意: この「セットアップ系の失敗チェック」は普通に行ってよい(strerror でメッセージを出すのも可)。禁止されているのは「**read/write の後の errno 参照**」だけである(第6章)。

### ✅ この章の要点

- サーバーの儀式: socket → setsockopt → fcntl → bind → listen →(poll)→ accept。
- accept は接続ごとに**新しい fd** を返す。listen fd は親機、client fd は子機。
- sin_port / sin_addr には htons / htonl 必須。
- recv/send は「一部だけ」がありうる。recv の 0 は切断の合図。
- すべてのシステムコールは失敗しうる。戻り値を必ず見る。

---

## 第4章 ブロッキングとノンブロッキング

Webserv 最大の関門がこの概念である。ここが腑に落ちれば、課題の要求の意味がすべて繋がる。

### 4.1 「ブロックする」とは

プログラムがある処理のところで**止まって、結果が返るまで先に進めなくなる**ことを「ブロックする」という。

電話の保留を想像してほしい。「少々お待ちください」と保留にされたら、あなたは受話器を持ったまま待つしかない。その間、他のことは何もできない。これがブロック状態である。

通常の(ブロッキングな)ソケットで recv() を呼ぶと:

```
recv() を呼ぶ
   ↓
データがまだ届いていない
   ↓
【プログラムがここで停止。データが来るまでずっと待つ】
   ↓
データ到着。ようやく次の行へ
```

### 4.2 なぜサーバーで致命的か

サーバーは多数のクライアントを**同時に**相手にする。ブロッキングだと:

```
クライアントAのrecv()で待機中...
   │
   ├─ その間にクライアントBが接続してきた → 気づけない
   ├─ クライアントCがデータを送ってきた → 処理できない
   └─ Aがずっと黙っていたら → サーバー全体が永遠に固まる
```

レストランで例えると、ウェイターが1番テーブルの前に突っ立って「ご注文がお決まりになるまでここで待ちます」と動かない状態。2番3番のお客が手を挙げても完全に無視。店は回らない。

### 4.3 ノンブロッキングとは

ノンブロッキングなソケットでは、recv() は**データが無ければ「今は何もないよ」とすぐ返り、止まらない**。

```
recv() を呼ぶ
   ↓
データがまだ届いていない
   ↓
「今は無い」とすぐ返る(止まらない!)
   ↓
プログラムは次へ進める。他のクライアントを見に行ける
```

ウェイターが「ご注文お決まりですか?」→ まだなら「では後ほど」とすぐ次のテーブルへ回るイメージ。全テーブルを並行して捌ける。

ソケットをノンブロッキングにする命令:

```cpp
#include <fcntl.h>
fcntl(fd, F_SETFL, O_NONBLOCK);
```

### 4.4 Mac 環境での注意

Linux には accept4() や SOCK_NONBLOCK という「生成時からノンブロッキング」の近道があるが、**Mac には存在しない**。Mac では accept() で得た fd を、後から fcntl で明示的にノンブロッキング化するしかない。

課題はこの事情を踏まえて fcntl の使用を許可しているが、**使えるフラグは F_SETFL / O_NONBLOCK / FD_CLOEXEC の3つだけ**と制限している。

実務上の結論: **「fd を得たら即 fcntl(fd, F_SETFL, O_NONBLOCK)」を習慣化する。** この書き方は Linux でもそのまま動くので、移植性も確保できる。

- listen ソケット: socket() の直後に
- クライアントソケット: accept() の直後に
- CGI のパイプ: pipe() の直後に(親側)

### 4.5 ノンブロッキングだけでは足りない

ここで新たな問題が生じる。ノンブロッキングにすると recv は止まらないが、「データが来たか」を知るには**何度も recv を呼び続ける**しかなくなる。

```
recv() → 無い
recv() → 無い
recv() → 無い     ← 延々と無駄な問い合わせ(ビジーウェイト)
recv() → 無い        CPU を 100% 無駄遣いする
```

ウェイターが全テーブルを猛スピードでぐるぐる回り続けて汗だくになる状態。これを解決するのが次章の poll() である。

### ✅ この章の要点

- ブロック = 結果が返るまでプログラムが止まること。サーバーでは1人の客に全員が道連れになる。
- ノンブロッキング = 「今は無い」とすぐ返る。fcntl(fd, F_SETFL, O_NONBLOCK) で設定。
- Mac では accept4/SOCK_NONBLOCK が無い。「fd を得たら即 fcntl」を習慣に。
- ノンブロッキング単体ではビジーウェイトに陥る。poll と組み合わせて初めて完成する。

---

## 第5章 I/O多重化と poll()

### 5.1 poll() は「見張りの外注」

poll() は OS にこう頼む関数である。

> 「この一覧のソケットたちを見張っておいて。**どれかに動きがあったら**そのとき起こして。それまで私は寝てるから」

自分でせかせか確認して回る(ビジーウェイト)のをやめ、**見張りを OS に任せる**。OS は「3番と7番が読めるようになりましたよ」と教えてくれるので、プログラムは教えられた fd だけを処理すればいい。

レストランの例えなら、各テーブルに**呼び出しボタン**を設置し、押されたテーブルにだけ向かう方式である。

### 5.2 Non-blocking I/O と I/O multiplexing は別物

混同しやすいが、この2つは**別の概念で、組み合わせて使う**。

| | Non-blocking I/O | I/O multiplexing |
|---|---|---|
| 何の話か | **1回の読み書き**の振る舞い | **複数fdを束ねた監視**の仕組み |
| 実現手段 | O_NONBLOCK 設定 | poll / select / epoll / kqueue |
| 答える問い | 「今すぐ読める? 無ければすぐ戻る」 | 「**どの** fd が今読める/書ける?」 |
| 単体での弱点 | いつ来るか知る手段がない | — |

なぜ両方必要か: poll が「読める」と言った直後でも、実際に recv するまでのわずかな間に状況が変わるなどの際どいケースがある。ソケットがブロッキングのままだと、その隙に recv が固まってサーバー全体が止まりうる。ノンブロッキングにしておけば、万一でも「今は無い」とすぐ返るので止まらない。

**「poll(多重化)で動きのある fd を知り、ノンブロッキングな fd に対してだけ読み書きする」** — この組み合わせが Webserv の心臓部である。

### 5.3 poll() の使い方

```cpp
#include <poll.h>

struct pollfd {
    int   fd;        // 監視したい fd
    short events;    // 何を監視するか (POLLIN / POLLOUT)
    short revents;   // 【OSが書き込む】実際に何が起きたか
};

std::vector<struct pollfd> pfds;   // 全fdの監視リスト

// 登録例: このfdの「読める」を監視したい
struct pollfd p;
p.fd = clientFd;
p.events = POLLIN;
p.revents = 0;
pfds.push_back(p);

// 監視実行: どれかに動きがあるまで待つ (最後の引数はタイムアウトms; -1は無限)
int ready = poll(&pfds[0], pfds.size(), -1);

// 結果確認: revents に印が付いている
for (size_t i = 0; i < pfds.size(); ++i) {
    if (pfds[i].revents & POLLIN)  { /* このfdは読める → recv してよい */ }
    if (pfds[i].revents & POLLOUT) { /* このfdは書ける → send してよい */ }
    if (pfds[i].revents & (POLLERR | POLLHUP)) { /* エラー/切断 → close */ }
}
```

主なイベントフラグ:

| フラグ | 意味 |
|---|---|
| POLLIN | 読める(データが届いている / listen fd なら新規接続がある) |
| POLLOUT | 書ける(送信バッファに空きがある) |
| POLLERR / POLLHUP / POLLNVAL | エラー / 相手が切断 / 無効なfd(reventsにのみ現れる) |

### 5.4 課題の要求: 「単一の poll()」

課題は「**1つの poll()(または同等物)で、listen を含む全ての I/O を駆動せよ**」と要求する。これは:

- サーバー全体で poll の呼び出し箇所(イベントループ)は**1つ**。
- そのループの監視リストに、**listen fd も、全クライアント fd も、CGI のパイプも、全部入れる**。
- クライアントごとに別の poll を持ったり、poll と select を混在させたりしない。

```
                    唯一の poll() ループ
    ┌──────────────────────────────────────────────┐
    │  監視リスト:                                   │
    │   [listen:8080] [listen:8081]  ← 待ち受け      │
    │   [client 5] [client 6] [client 7] ← ブラウザ達 │
    │   [cgi-pipe 8] [cgi-pipe 9]    ← CGI          │
    └──────────────────────────────────────────────┘
          │ poll() が「動きのあったfd」を教える
          ▼
     該当fdの処理へディスパッチ(1ステップだけ進めて戻る)
```

### 5.5 POLLOUT の付け外し — CPU を守る規律

**POLLOUT を常時立ててはいけない。** ソケットの送信バッファはたいてい空いているので、POLLOUT を立てっぱなしにすると poll が「書けるよ!書けるよ!」と即座に返り続け、ループが空回りして CPU を食い潰す。

正しい規律:

```
送るデータが無い間   → events = POLLIN のみ
送るデータができた   → events = POLLIN | POLLOUT に変更
全部送り切った       → events = POLLIN に戻す
```

「送信待ちデータがあるときだけ POLLOUT」——これを徹底する。

### 5.6 poll / select / epoll / kqueue の使い分け

課題は poll 以外の同等関数も許可している。比較:

| 関数 | 対応OS | 特徴 | 本書の評価 |
|---|---|---|---|
| select | ほぼ全OS | 最古。fd数上限(1024)、毎回集合を組み直す手間 | 扱いにくい |
| **poll** | **Mac/Linux両対応** | **配列で管理、シンプル、fd数上限なし** | **推奨** |
| epoll | **Linux専用** | 大量接続で高速 | Macでは使えない |
| kqueue | Mac/BSD専用 | Macでの高速版。ただしAPIが複雑(登録/取得が別、読み書き別イベント) | 学習コスト高 |

**Mac 開発なら poll 一択でよい。** epoll は Mac に存在せずコンパイルすら通らない。kqueue は高性能だが、この課題の負荷(数百接続程度)では性能差が出ず、学習コストに見合わない。poll なら評価環境が Linux でもそのまま動く。

なお、選んだ関数に付随するマクロ(select の FD_SET 等、poll の POLLIN 等)は自由に使ってよいと課題が明記している。

### ✅ この章の要点

- poll = 複数 fd の見張りを OS に外注し、動きのあったものだけ教えてもらう仕組み。
- ノンブロッキング(1回のI/Oの性質)と多重化(束ねた監視)は別概念。両方揃えて完成。
- イベントループは**プログラム全体で1つ**。listen も client も CGI パイプも全部載せる。
- POLLOUT は送信待ちデータがあるときだけ。常時 ON は CPU 空回りの元。
- Mac 開発は poll 一択。epoll は Mac に無い。

---

## 第6章 禁じられた errno と readiness ルール

0点ルールの中でも特に誤解しやすい2つを、この章で完全に理解する。

### 6.1 errno とは

errno は、システムコールが失敗したとき「**なぜ失敗したか**」を OS が書き残すグローバル変数である。一般的な C/C++ のネットワークコードでは、こう書くのが定石とされる。

```cpp
// ※ 一般的なコードの例。Webserv では禁止!
ssize_t n = recv(fd, buf, size, 0);
if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 「まだデータが無いだけ」→ 後で再試行
    } else if (errno == ECONNRESET) {
        // 「接続が切れた」→ close する
    }
}
```

recv の失敗後に errno を見て理由を判別し、**挙動を分岐**している。Webserv ではこれが禁止である。

### 6.2 なぜ禁止なのか

ノンブロッキング環境での errno は、規格上きわめて危うい。

1. **errno は成功時にクリアされない。** 前の操作の古い値が残っていることがあり、誤判定の元になる。
2. **読むまでの間に上書きされうる。** recv が -1 を返してから errno を読むまでに別の処理(ログ出力など)が挟まると、その処理が errno を書き換えてしまう。

42 がこのルールを課すのは、こうした不安定な判定に頼らず、**もっと信頼できる情報源だけで動くサーバー**を書かせるためである。その信頼できる情報源が poll の通知である。

### 6.3 正しい書き方: 戻り値と poll だけで判断する

判断材料を3つに絞る。

```cpp
ssize_t n = recv(fd, buf, sizeof(buf), 0);

if (n == 0) {
    // 相手が接続を閉じた(確定的な意味を持つ) → close 処理へ
    closeConnection(fd);
    return;
}
if (n < 0) {
    // 今は読めない。理由は詮索しない(errnoを見ない)。
    // 何もせず return → 次に poll が「読める」と言ったら再試行される
    return;
}
// n > 0: n バイト読めた。処理する。
```

なぜ「何もしない」で成立するのか: ノンブロッキング + poll の組み合わせでは、「今回ダメだった → 次に poll が教えてくれるまで待つ」だけで十分だから。poll が次のループで改めて「読めるよ」と言ってくれる。**errno で理由を調べる必要そのものが消滅する**。

send 側も同じ発想:

```cpp
ssize_t n = send(fd, outBuf.data(), outBuf.size(), 0);
if (n < 0)
    return;              // 今は書けない。次の POLLOUT を待つ
outBuf.erase(0, n);      // 送れた分だけ削る(部分送信対応)
```

### 6.4 readiness ルールの正確な範囲

課題文の該当箇所を分解する。

> I/O that can wait for data (sockets, pipes/FIFOs, etc.) must be non-blocking and driven by a single poll() (or equivalent). Calling read/recv or write/send on these descriptors without prior readiness will result in a grade of 0. **Regular disk files are exempt.**

- **対象**: 「データを待ちうる」fd = ソケットとパイプ。これらは (a) ノンブロッキング化し (b) 単一 poll で駆動し (c) readiness(pollの「読める/書ける」通知)を受けてからのみ read/write する。破れば 0 点。
- **例外**: 通常のディスクファイル。理由は、中身が既に全部ディスクにあり「相手待ち」が原理的に発生しないから。しかも poll はディスクファイルに対して常に「読める」としか答えないので、監視する意味もない。

実装上の意味:

```cpp
// ソケット: poll 経由でないと触れない
recv(clientFd, ...);   // ← 必ず POLLIN 確認後

// ディスクファイル: poll 不要。直接読んでよい(課題公認の例外)
int f = open("./www/index.html", O_RDONLY);
read(f, buf, size);
close(f);
```

### 6.5 errno 禁止の正確な範囲

禁止は「**read/write 操作の後に** errno を見て**サーバーの挙動を変える**こと」。以下は問題ない:

- socket / bind / listen などセットアップ系の失敗時にエラーメッセージを出す(strerror 含む)
- 起動時の設定エラーで終了する

つまり「稼働中の読み書きの結果判定に errno を使うな」がルールの核心である。

### ✅ この章の要点

- recv/send の後の errno 分岐は禁止。判断は「戻り値(0=切断 / 正=成功分 / 負=次のpollを待つ)」+「pollの通知」のみ。
- 負の戻り値のときは**何もしないで return** が正解。次の poll が面倒を見てくれる。
- readiness ルールの対象はソケットとパイプ。ディスクファイルは公認の例外。
- セットアップ系の errno/strerror 使用は禁止対象外。

---

## 第7章 C++98 サバイバルガイド

この課題は C++98 縛りである。C++ 経験が浅い読者向けに、「この課題で使う道具」に絞って解説する。

### 7.1 使えないもの(モダンC++の機能)

ネットの記事や AI の出力には C++11 以降のコードが混ざりがちなので、**見分けられるようになる**ことが大事。

| 使えない (C++11以降) | 代わりにこう書く (C++98) |
|---|---|
| `auto it = m.begin();` | `std::map<int,std::string>::iterator it = m.begin();` |
| `for (auto& x : v)` | `for (size_t i = 0; i < v.size(); ++i)` |
| `nullptr` | `NULL` |
| `std::unique_ptr` / `shared_ptr` | 生ポインタ + 自前で delete |
| `std::to_string(n)` | `std::ostringstream` を使う(下記) |
| `std::stoi(s)` | `std::atoi(s.c_str())` または istringstream |
| 初期化リスト `{1,2,3}` | push_back を並べる |
| `enum class` | 普通の `enum` |
| ラムダ式 `[](){}` | 関数 or 関数オブジェクト |

数値→文字列変換(頻出。Content-Length などで使う):

```cpp
#include <sstream>
std::string toString(size_t n) {
    std::ostringstream oss;
    oss << n;
    return oss.str();
}
```

### 7.2 この課題で主力になる STL

**std::string** — バイト列の入れ物として最重要。受信バッファ・送信バッファ・パース対象、すべて string で持つ。

```cpp
std::string buf;
buf.append(data, n);          // 受信分を末尾に追加
buf.erase(0, n);              // 先頭 n バイトを削る(送信済み分)
size_t pos = buf.find("\r\n"); // 行末を探す(npos なら見つからない)
std::string line = buf.substr(0, pos);  // 部分文字列
```

注意: HTTP のボディはバイナリ(画像など)も来る。`c_str()` ではなく `data()` と `size()` で長さ管理し、「文字列 = NUL終端」という思い込みを捨てること。

**std::map** — 「キー→値」の対応表。ヘッダ(名前→値)、fd→Connection、拡張子→CGIパスなどで多用。

```cpp
std::map<int, Connection*> conns;
conns[fd] = new Connection(fd);           // 追加
std::map<int, Connection*>::iterator it = conns.find(fd);
if (it != conns.end()) { /* 見つかった */ }
conns.erase(fd);                          // 削除
```

**std::vector** — 可変長配列。pollfd の配列、ServerConfig の一覧など。

**std::set** — 集合。allowedMethods など「含まれるか?」を見る用途。

### 7.3 クラス設計の基本形

この課題で書くクラスの典型形。Orthodox Canonical Form(コンストラクタ/コピーコンストラクタ/代入演算子/デストラクタ)は 42 の他課題で学ぶ流儀に従う。

```cpp
class Connection {
private:
    int         _fd;
    std::string _inBuf;
    // コピー禁止にしたい場合は private で宣言だけしておく(C++98流)
    Connection(const Connection&);
    Connection& operator=(const Connection&);
public:
    explicit Connection(int fd) : _fd(fd) {}   // 初期化リストで初期化
    ~Connection() { if (_fd >= 0) close(_fd); } // デストラクタで資源返却
    int fd() const { return _fd; }              // 読み取り専用は const
};
```

ポイント:

- **デストラクタで資源(fd/メモリ)を返す**習慣。これが C++ 流のリーク防止。
- fd を持つクラスは**コピーを禁止する**(コピーされると同じ fd を2箇所が close してしまう)。C++98 ではコピーコンストラクタと代入演算子を private 宣言して塞ぐ。
- メンバ変数は `_` 始まり(42の慣習)、変更しない関数には const。

### 7.4 new/delete とメモリ管理

スマートポインタが無いので、`new` したものは自分で `delete` する。

```cpp
Connection* c = new Connection(fd);
conns[fd] = c;
// ...使い終わったら...
conns.erase(fd);
delete c;         // 忘れるとメモリリーク
```

規律: **「new する場所」と「delete する場所」をペアで設計する。** 本書の設計では、Connection と CgiProcess の生成/破棄は EventLoop が一元管理する(第15章)。「誰が所有者(owner)か」を常に1箇所に決めておけば、リークも二重解放も防げる。

### 7.5 例外とクラッシュ耐性

「いかなる状況でもクラッシュ禁止」を C++ で守るための最低限:

```cpp
int main(int argc, char** argv) {
    try {
        // サーバー全体
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << std::endl;
        return 1;   // クラッシュではなく正常な終了コードで終わる
    } catch (...) {
        return 1;
    }
}
```

- new は失敗すると std::bad_alloc を投げる。最上位の try/catch で受け止め、崩れずに終了(または接続を閉じて継続)する。
- 配列アクセスは範囲を確認してから。NULL ポインタはデリファレンス前に必ずチェック(第17章の Router::match が典型)。

### ✅ この章の要点

- auto / 範囲for / nullptr / スマートポインタ / to_string は使えない。C++98 の書き換えパターンを覚える。
- string(バイト列)、map(対応表)、vector(配列)、set(集合)が主力。
- fd を持つクラスはコピー禁止 + デストラクタで close。new/delete は所有者を1箇所に決める。
- main の最上位に try/catch。クラッシュせず終わる構えを最初から入れる。


---
---

# 第II部 HTTP編

---

## 第8章 HTTPメッセージの生の姿

サーバーが受け取るのも返すのも、結局は**決まった書式のテキスト(バイト列)**である。ライブラリなしで自作するのだから、この生の姿を正確に知る必要がある。

### 8.1 リクエストの構造

ブラウザが `http://localhost:8080/index.html` にアクセスすると、ソケットにはこういうバイト列が届く。

```
GET /index.html HTTP/1.1\r\n          ← リクエストライン
Host: localhost:8080\r\n              ← ヘッダ(名前: 値)
User-Agent: Mozilla/5.0 ...\r\n       ←   〃
Accept: text/html,...\r\n             ←   〃
\r\n                                  ← 空行 = ヘッダの終わり
(ボディがあればここに続く)
```

構造の決まり:

1. **リクエストライン**: `メソッド 半角スペース ターゲット 半角スペース HTTP/バージョン`
2. **ヘッダ群**: `名前: 値` を1行ずつ。**名前は大文字小文字を区別しない**(Host も host も HOST も同じ扱いにする)。
3. **空行(\r\n だけの行)**: ヘッダの終わりの合図。
4. **ボディ**: POST のときなど。長さは Content-Length ヘッダか chunked 形式で決まる(8.3節)。

**改行は \r\n(CRLF)である。** \n だけではない。パースで `find("\r\n")` を使うのはこのため。

### 8.2 レスポンスの構造

サーバーが返すのも同じ形式で、1行目だけ違う。

```
HTTP/1.1 200 OK\r\n                   ← ステータスライン
Content-Type: text/html\r\n
Content-Length: 137\r\n               ← ボディのバイト数(超重要)
Connection: keep-alive\r\n
\r\n
<html><body>Hello!</body></html>      ← ボディ(Content-Length バイト分)
```

**Content-Length は正確に。** ブラウザはこの数字を信じて「ここまでがボディ」と判断する。実際のボディ長とずれていると、表示が切れる・次のリクエストと混ざる等の不可解な不具合になる。HttpResponse クラスで setBody 時に自動計算するのが安全(第17章)。

### 8.3 ボディの長さの決まり方 — 2方式

リクエスト/レスポンスのボディの「終わり」を知る方法は2つある。

**方式1: Content-Length** — ヘッダで「ボディは N バイト」と宣言。N バイト読んだら終わり。

**方式2: Transfer-Encoding: chunked** — 長さが事前に分からないとき、小分け(チャンク)にして送る方式。

```
4\r\n            ← 次のチャンクは4バイト(16進数)
Wiki\r\n         ← 4バイトのデータ
5\r\n            ← 次は5バイト
pedia\r\n
0\r\n            ← サイズ0 = 終わりの合図
\r\n
```

→ 繋げると "Wikipedia"。

課題は「**chunked リクエストをサーバー側で un-chunk(復元)せよ**」と要求する。特に CGI へ渡す際は un-chunk 済みのボディを渡す義務がある。デコードのアルゴリズムは第16章で実装する。

### 8.4 multipart/form-data — ファイルアップロードの形

HTML の `<form enctype="multipart/form-data">` からファイルを送ると、ボディはこういう形になる。

```
Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryABC123

------WebKitFormBoundaryABC123\r\n
Content-Disposition: form-data; name="file"; filename="cat.png"\r\n
Content-Type: image/png\r\n
\r\n
(ここにファイルの生バイナリ)\r\n
------WebKitFormBoundaryABC123--\r\n      ← 末尾は boundary + "--"
```

- ヘッダの boundary という文字列が「区切り線」として使われる。
- パースの手順: (1) Content-Type ヘッダから boundary を取り出す → (2) ボディを boundary で区切る → (3) 各パートの filename とデータ部分を取り出す → (4) データを upload_store のフォルダに書き出す。
- **データはバイナリでありうる**。文字列関数の NUL 終端に頼らず、位置とサイズで扱うこと。

最小実装の割り切り: 完全な multipart パーサは大変なので、「boundary で分割し、最初のファイルパートを保存する」程度でも評価のデモは成立する。ただし filename の取り出しと、ヘッダ部/データ部の境目(空行 \r\n\r\n)の処理は正確に。

### ✅ この章の要点

- HTTP は「1行目 + ヘッダ群 + 空行 + ボディ」のテキスト形式。改行は \r\n。
- ヘッダ名は大文字小文字を区別しない。Content-Length は正確に。
- ボディ長は Content-Length か chunked。chunked は un-chunk する義務がある。
- アップロードは multipart/form-data。boundary で区切ってファイル部分を取り出す。

---

## 第9章 HTTP/1.1 で実装すべき範囲

### 9.1 なぜ 1.1 を目標にするか

課題は「HTTP/1.0 を参照点として推奨(強制ではない)」と言う。しかし本書は **1.1 を目標にする**。理由:

1. **実ブラウザは 1.1 で話す。** 課題の「標準ブラウザと互換であること」を満たすには、Host ヘッダ・keep-alive・chunked を扱えないと詰まる。
2. **chunked の un-chunk は課題の明示要件**であり、これは 1.1 の機能。対応する時点で 1.1 の世界に入っている。
3. 1.0 で作って後から 1.1 要素を足すより、最初から 1.1 を名乗る方が二度手間がない。

### 9.2 1.0 と 1.1 の違い(この課題に関係する範囲)

| 項目 | HTTP/1.0 | HTTP/1.1 |
|---|---|---|
| 接続 | 1リクエストごとに切断 | デフォルトで維持(keep-alive) |
| Host ヘッダ | 任意 | **必須**(欠落は 400) |
| chunked | なし | あり(un-chunk 義務) |

### 9.3 実装する 1.1 の要素(これだけでよい)

RFC の完全実装は不要。課題自身が「RFC の一部だけを提供することを意図的に選んだ」と明言している。対応すべきは:

- リクエストラインの `HTTP/1.1` を受け、レスポンスも `HTTP/1.1` で返す
- Host ヘッダの受領(1.1 で欠落なら 400)
- Content-Length に基づくボディ読み取り
- chunked リクエストの un-chunk
- keep-alive: 送信完了後、`Connection: close` が無ければ接続を維持して次のリクエストを待つ
- レスポンスへの正確な Content-Length 付与

**対応しなくてよいもの**: パイプライニングの厳密な実装、キャッシュ制御、Range リクエスト、圧縮、HTTP/2 以降。

### 9.4 実装順序のコツ: 「毎回closeする1.1」から始める

keep-alive は接続の使い回し(リクエストのリセット、状態の巻き戻し)が絡んで難易度が上がる。おすすめの段階戦略:

1. **フェーズ1**: `HTTP/1.1` を名乗るが、全レスポンスに `Connection: close` を付けて毎回切断する。1.0 並みの単純さで土台を固める。
2. **フェーズ2**: 安定後、keep-alive を有効化。送信完了 → HttpRequest をリセット → READING 状態に戻る、の流れを足す。

### ✅ この章の要点

- 目標は HTTP/1.1(ブラウザ互換と chunked 要件のため)。ただし RFC 全部ではなく中核のみ。
- 必須: Host / Content-Length / chunked un-chunk / keep-alive / 正確なステータス。
- 最初は「毎回 close する 1.1」で通し、後から keep-alive を足すのが安全。

---

## 第10章 ステータスコードの正確さ

課題の要件「Your HTTP response status codes must be accurate」に応えるため、状況→コードの対応を固定する。

### 10.1 コード対応表(この課題で使うもの)

| コード | 意味 | 発生場面 |
|---|---|---|
| 200 OK | 成功 | GET 成功、CGI 正常出力 |
| 201 Created | 作成した | アップロード成功 |
| 204 No Content | 成功・返す中身なし | DELETE 成功 |
| 301/302 | リダイレクト | location の return 指定 |
| 400 Bad Request | リクエストが壊れている | パース不正、Host 欠落(1.1) |
| 403 Forbidden | 禁止 | autoindex off のディレクトリ、権限なし、**パストラバーサル検出** |
| 404 Not Found | 無い | ファイル不在、一致する location 無し |
| 405 Method Not Allowed | メソッド不許可 | allowedMethods 外(**Allowヘッダ必須**) |
| 408 Request Timeout | クライアントが遅すぎ | 接続タイムアウト |
| 413 Payload Too Large | ボディがでかすぎ | client_max_body_size 超過 |
| 500 Internal Server Error | サーバー内部の失敗 | CGI 異常など |
| 501 Not Implemented | 未対応メソッド | GET/POST/DELETE 以外 |
| 504 Gateway Timeout | CGI が返事しない | CGI タイムアウトで強制終了したとき |

### 10.2 405 には Allow ヘッダを必ず付ける

HTTP 仕様(RFC 7231)は、405 を返すとき **Allow ヘッダで「許可されるメソッド」を示すことを必須(MUST)** としている。

```
HTTP/1.1 405 Method Not Allowed\r\n
Allow: GET, POST\r\n
Content-Length: ...\r\n
\r\n
...
```

値は location の allowedMethods をカンマ区切りにしたもの。405 判定の時点で location は分かっているので実装は容易。NGINX も付けてくるので、挙動比較でも揃う。

### 10.3 エラーページの二段構え

各エラーコードについて、**設定の error_page 指定があればそのファイルを、無ければサーバー内蔵のデフォルト HTML を返す**。デフォルトは最小限でよい:

```cpp
std::string defaultErrorPage(int code, const std::string& reason) {
    std::ostringstream oss;
    oss << "<html><head><title>" << code << " " << reason
        << "</title></head><body><h1>" << code << " " << reason
        << "</h1><hr><p>webserv</p></body></html>";
    return oss.str();
}
```

### ✅ この章の要点

- 状況→コードの対応を表の通り統一する。「とりあえず200/500」は減点対象。
- 405 には Allow ヘッダ必須。値は allowedMethods のカンマ区切り。
- エラーページは「設定優先、無ければ内蔵デフォルト」の二段構え。

---
---

# 第III部 設計編

---

## 第11章 全体アーキテクチャ — 4層で考える

### 11.1 層に分ける理由

サーバーを4層に分離する。各層は下の層のインターフェイスだけに依存し、上の詳細を知らない。この分離が (1) 2人の並行作業の境界になり、(2) ノンブロッキングの規律を I/O 層に閉じ込め、(3) テストと差し替えを容易にする。

```
   ┌─────────────────────────────────────────────┐
   │ HTTP層: リクエスト解釈・レスポンス生成         │ ← 担当B
   │  HttpRequest / HttpResponse / Router /       │
   │  RequestHandler                              │
   ├─────────────────────────────────────────────┤
   │ CGI層: fork+execve、パイプI/O                 │ ← 担当B (pollとの接点はAと協調)
   │  CgiProcess                                  │
   ├─────────────────────────────────────────────┤
   │ I/O層: ソケット、単一poll、ノンブロッキング送受信│ ← 担当A(基盤コア)
   │  EventLoop / ListenSocket / Connection       │
   ├─────────────────────────────────────────────┤
   │ 設定層: .confのパースと保持(不変データ)        │ ← 担当A
   │  Config / ServerConfig / LocationConfig      │
   └─────────────────────────────────────────────┘
```

**層分離の鉄則:**

- I/O 層は「**いつ**読めるか/書けるか」だけを管理し、HTTP の**意味**を一切知らない。
- HTTP 層は「バイト列をもらってバイト列を返す」だけで、**ソケットを直接触らない**。
- CGI のパイプを poll に載せる部分だけが、層をまたぐ唯一の接点。

### 11.2 静的リクエストのデータフロー

```
 ①poll: listen fdが読める
    │ accept → Connection生成、client fdを登録(POLLIN)
 ②poll: client fdが読める
    │ recv → HttpRequest.consume() (逐次パース)
    │ …何度か繰り返して…リクエスト完成
 ③Router::match で location 決定 → RequestHandler::handle
    │ ファイルを読んで(★poll不要:ディスクファイル例外)
    │ HttpResponse 完成 → serialize して送信バッファへ
    │ このfdの監視を POLLOUT 込みに変更
 ④poll: client fdが書ける
    │ send(部分送信対応)…送り切ったら
 ⑤keep-alive判定 → READING に戻る or close
```

### 11.3 CGI リクエストのデータフロー(非同期)

CGI はパイプ(待たされるfd)を扱うため、その場で結果を待てない。**結果は後から poll 経由で届く**。

```
 ①〜②は静的と同じ
 ③handle が「CGI対象」と判定
    │ CgiProcess::start (fork+execve, パイプ2本)
    │ パイプfdをノンブロッキング化し、pollに登録(attachCgi)
    │ handle は「CGI_STARTED」を返して一旦抜ける
    │ Connection は CGI_RUNNING 状態で待機
 ④poll: CGIパイプ(書ける) → POSTボディを少しずつ書く→書き切ったらclose(EOF通知)
 ⑤poll: CGIパイプ(読める) → 出力を蓄積…readが0(EOF)でCGI完了
 ⑥出力からレスポンスを組み立て、Connection::onCgiDone に渡す
    │ Connection は WRITING へ → 以降は静的と同じ送信フロー
 ⑦waitpid(WNOHANG)で子プロセス回収、パイプfdをpollから外す
```

### ✅ この章の要点

- 4層(設定/I/O/HTTP/CGI)に分け、I/O層は「いつ」、HTTP層は「何を」だけを知る。
- 静的処理は handle の中で同期完結(ディスクファイル例外のおかげ)。
- CGI だけは非同期: パイプを poll に載せ、結果は後から onCgiDone で届く。

---

## 第12章 状態機械という考え方

### 12.1 なぜ状態機械が必要か

ノンブロッキングサーバーでは、1つの処理(リクエスト受信→処理→送信)が**一気に完了しない**。データは少しずつ届き、送信も少しずつしか進まない。だから各接続について「**いまどこまで進んだか**」を覚えておき、poll のイベントが来るたびに**1ステップだけ進めて戻る**、という書き方になる。この「どこまで進んだか」が状態(state)である。

同期的なコード(上から下に流れる)との対比:

```
【同期(書けない)】            【状態機械(こう書く)】
recv 全部           ←ブロック   poll「読める」→ 少しrecv → 状態更新 → 戻る
処理                          poll「読める」→ 少しrecv → 完成!→処理→状態=WRITING
send 全部           ←ブロック   poll「書ける」→ 少しsend → 状態更新 → 戻る
                              poll「書ける」→ 残りsend → 完了 → 状態=READING/CLOSING
```

### 12.2 Connection の状態遷移(全体図)

```
                        ┌─────────────┐
        accept で生成 →  │   READING   │ ←──────────────┐
                        └──────┬──────┘                 │
                     リクエスト完成                        │ keep-alive
              ┌────────────┴─────────────┐              │ (reqをリセット)
              │静的処理                    │CGI対象        │
              ▼                          ▼              │
        (HANDLING: 一瞬)          ┌─────────────┐        │
              │                  │ CGI_RUNNING │        │
              │                  └──────┬──────┘        │
              │                    onCgiDone            │
              ▼                          │              │
        ┌─────────────┐ ◀───────────────┘              │
        │   WRITING   │────── 送信完了 ──────────────────┤
        └──────┬──────┘                                 │
               │ Connection: close / エラー              │
               ▼                                        │
        ┌─────────────┐                                 │
        │   CLOSING   │  fdをpollから外してclose、破棄     │
        └─────────────┘                                 
```

| 状態 | 監視するイベント | やること・遷移 |
|---|---|---|
| READING | POLLIN | recv → consume。完成したら handle を呼ぶ。同期完了→WRITING、CGI→CGI_RUNNING。パース不正はエラーレスポンスを組んで WRITING |
| HANDLING | (内部) | 同期処理の一瞬の状態。実装上は READING から直接 WRITING に飛ばしてよい |
| CGI_RUNNING | (CGIパイプ側のイベント) | クライアントfdは待機。CGIパイプの進行は CgiProcess が担う。onCgiDone で WRITING へ |
| WRITING | POLLOUT | send(部分送信)。送り切ったら keep-alive なら READING(reqリセット)、close なら CLOSING |
| CLOSING | — | poll から外し close、Connection 破棄 |

### 12.3 「一回の結果」と「持続する状態」— 2種類の enum

設計上、性質の違う2つの enum を使い分ける。

- **HandleResult(一回の呼び出しの結果)**: handle() を呼んだその瞬間、「結果ができた(RESPONSE_READY)」のか「CGIに切り替わった(CGI_STARTED)」のかを呼び出し側に伝える。使い捨ての情報。
- **ConnState(持続する状態)**: その接続がいまどのフェーズにいるか。Connection オブジェクトが生きている間ずっと保持される。

連携の形:

```cpp
HandleResult r = handler.handle(_req, *loc, *_server, *this, resp);
if (r == RESPONSE_READY) {
    _outBuf = resp.serialize();
    _state = WRITING;              // 一回の結果を受けて、持続状態を更新
} else {                            // CGI_STARTED
    _state = CGI_RUNNING;
}
```

CGI 側にも独自の進行状態(CgiState: 入力書き込み中→出力読み取り中→完了)がある(第18章)。

### ✅ この章の要点

- ノンブロッキング = 処理を一気に終えられない世界。だから「どこまで進んだか」= 状態を持つ。
- poll のイベント1回につき1ステップだけ進めて戻る。決してブロックしない。
- HandleResult(瞬間の結果)と ConnState(持続する状態)は役割が違う。両方使う。

---

## 第13章 境界面(契約)とチーム開発

### 13.1 境界面 = 合体場所の形を先に決める

2人で別々のパーツを作って後で合体させる。このとき**合体する場所の形を、作り始める前に決めておく**——これが境界面の合意である。

USB の規格を思い浮かべるとよい。ケーブルを作る会社とポートを作る会社が別々でも、差し込み口の形(規格)を両者が守っているから、後で挿せば必ず繋がる。決めずに作ると、いざ繋ぐときに形が合わずどちらかが作り直しになる。

決めるのは「**2つのパーツが呼び合う関数の、正確な形(引数と戻り値)**」。中身(実装)はまだ書かない。形だけを、共有ヘッダ(webserv.hpp)に書いて合意する。

### 13.2 合意すべき4点

| # | 境界面 | 形 | 誰が呼ぶ→誰が実装 |
|---|---|---|---|
| 1 | リクエスト処理の依頼 | `HandleResult handle(req, loc, srv, conn, out)` | 基盤(Connection)が呼ぶ → アプリが実装 |
| 2 | CGIパイプの登録 | `EventLoop::attachCgi(pipeFd, cgiProcess)` | CGI層が呼ぶ → 基盤が実装 |
| 3 | CGI完了の通知 | `Connection::onCgiDone(response)` | CGI層が呼ぶ → 基盤が実装 |
| 4 | 設定の引き方 | `Config::findServer(host, port)` / `Router::match(srv, path)` | 両層が呼ぶ |

この4点が固まれば、基盤担当は handle の中身を知らずに呼び出しコードを書け、アプリ担当は呼び出され方を知らずに中身を書ける。**結合は差し替えるだけ**になる。

### 13.3 結合コードの実物(NULLチェック込み)

基盤側の Connection がリクエスト完成時に書くコード。**Router::match の NULL チェックを忘れるとクラッシュ = 0点**なので、この形をそのまま使う。

```cpp
// Connection::onReadable() の中、_req.isComplete() の後
HttpResponse resp;
const LocationConfig* loc = Router::match(*_server, _req.path());
if (loc == NULL) {                          // 一致 location 無し → 404
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
} else {                                    // CGI_STARTED
    _state  = CGI_RUNNING;                  // 送信は onCgiDone 後
}
```

### 13.4 チーム運用の型

- **前半(土台づくり)は担当固定**: 土台クラス(イベントループ、HTTPパーサ)は一人が一貫した思想で作り切る方が質も速度も上がる。細切れ分担は設計が継ぎ接ぎになる。
- **後半(肉付け)はチケット制**: エラーページ整備、autoindex の見た目、デモ用 conf、テスト作成などの独立タスクは、Issues 化して手の空いた人が拾う。
- **スタブで先に結合する**: 相方の handle が未完成でも、「固定の 200 レスポンスを返すだけのスタブ」を置けば、基盤は本物のブラウザで動作確認できる。相方の本物ができたら差し替えるだけ。
- **設計書と webserv.hpp が食い違ったら、実装が進んでいる webserv.hpp を正とする。** 変更は必ず相方に共有。
- **お互いのコードを週に数回読み合う**: 評価では「自分が書いていない部分も説明できる」ことが問われる(第21章)。

### ✅ この章の要点

- 境界面 = 呼び合う関数の形。作る前に webserv.hpp で合意し、中身は各自が独立に書く。
- 合意は4点: handle / attachCgi / onCgiDone / findServer・match。
- Router::match の NULL チェックは必須(クラッシュ=0点)。
- 前半は担当固定、後半はチケット制。スタブで早期に結合する。


---
---

# 第IV部 実装編

各コンポーネントを「何を・なぜ・どう書くか」の順で解説する。コードは方針を示す骨格であり、そのまま写すのではなく理解して自分の手で書くこと(評価で説明できることが合格条件である)。

---

## 第14章 設定ファイルとパーサ

### 14.1 設定ファイルの書式(本書の採用形)

NGINX 風の簡易書式。デモ用 .conf の出発点:

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

### 14.2 データ構造(パース結果の器)

```cpp
struct LocationConfig {
    std::string                        path;           // "/", "/upload" など
    std::set<std::string>              allowedMethods;
    std::string                        root;
    std::string                        index;
    bool                               autoindex;
    std::pair<int, std::string>        redirect;       // 無ければ first==0
    std::string                        uploadStore;
    std::map<std::string, std::string> cgi;            // ".py" → "/usr/bin/python3"
};

struct ServerConfig {
    std::string                 host;
    int                         port;
    std::map<int, std::string>  errorPages;
    size_t                      clientMaxBodySize;
    std::vector<LocationConfig> locations;
};
```

### 14.3 パーサの実装方針

複雑な字句解析は不要。**行/トークン単位の素朴な読み取り**で十分。

1. ファイル全体を読み、`{` `}` `;` を区切りとしてトークン化(空白・改行で分割)。
2. `server` → `{` を見たら ServerConfig を開始。`location <path>` → `{` で LocationConfig を開始。
3. ディレクティブ(listen, root, ...)はキーワード → 値 → `;` の並びとして読む。
4. `}` でブロックを閉じる。

守るべき検証(**起動時に弾く**。稼働中に不正設定でクラッシュさせない):

- 未知のディレクティブ、`;` 忘れ、`{}` の不整合 → エラー表示して終了。
- listen の重複、ポート範囲(1〜65535)の確認。
- client_max_body_size の単位(10M → 10*1024*1024)を数値に正規化。
- 引数で .conf が渡されなければ既定パス(例: ./config/default.conf)を読む。

### 14.4 落とし穴

- **location の最長一致**のために、locations はパースの後で「path が長い順」に並べておくと Router::match の実装が単純になる(前から見て最初に前方一致したもの = 最長一致)。
- 相対パス(./www/...)は「webserv を起動したディレクトリ基準」になる。評価時にどこから起動するかを意識して conf を書く。

### ✅ この章の要点

- パーサはトークン単位の素朴な実装で足りる。検証は起動時に全部やる。
- location は長い順に整列しておくと最長一致が楽。
- 単位(M/K)の正規化、既定パス対応を忘れない。

---

## 第15章 基盤コアの実装 — EventLoop / ListenSocket / Connection

担当Aの本丸。**動く echo サーバーを3クラスに整理する**アプローチで進める(ゼロから書かない)。

### 15.1 進め方の全体像(常に動くものを保つ)

```
Step1  echoサーバー(単一ファイル)          ← 完成済みの出発点
Step2  3クラスに分割(動作は echo のまま)
Step3  複数ポート対応(設定はハードコードでよい)
Step4  ConnState 状態機械化
Step5  handle() のスタブを繋ぐ → ブラウザで"Hello"が出る
Step6  設定パーサ接続・タイムアウト追加
```

各ステップで必ず動作確認してから次へ。壊れたらどのステップが原因か即分かる。

### 15.2 ListenSocket

```cpp
class ListenSocket {
    int                 _fd;
    const ServerConfig* _server;
public:
    bool setup(const ServerConfig& srv) {
        _server = &srv;
        _fd = socket(AF_INET, SOCK_STREAM, 0);
        if (_fd < 0) return false;
        int opt = 1;
        if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            return false;
        if (fcntl(_fd, F_SETFL, O_NONBLOCK) < 0) return false;

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(static_cast<uint16_t>(srv.port));
        if (bind(_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
            return false;
        if (listen(_fd, SOMAXCONN) < 0) return false;
        return true;
    }
    // POLLIN 時に呼ばれる。溜まった接続を全部受ける。
    int acceptClient() {
        int cfd = accept(_fd, NULL, NULL);
        if (cfd < 0) return -1;                     // もう無い(errnoは見ない)
        if (fcntl(cfd, F_SETFL, O_NONBLOCK) < 0) {  // ★fdを得たら即ノンブロッキング
            close(cfd);
            return -1;
        }
        return cfd;
    }
    int fd() const { return _fd; }
    const ServerConfig* server() const { return _server; }
};
```

複数ポート = 設定の listen の数だけ ListenSocket を作り、**全部を同じ poll に載せる**。どの listen fd で accept したかによって、その Connection の所属 ServerConfig が決まる(ポートごとに違うコンテンツを配る仕組みの正体)。

### 15.3 Connection — 部分送受信と状態機械

核心は2つ: (a) recv/send は一部だけしか進まない前提のバッファ管理、(b) ConnState による進行管理。

```cpp
class Connection {
    int                 _fd;
    ConnState           _state;
    std::string         _inBuf;      // 受信の生バイト置き場
    std::string         _outBuf;     // 送信待ちバイト
    HttpRequest         _req;
    const ServerConfig* _server;
    time_t              _lastActive;
public:
    // POLLIN → ここが呼ばれる
    void onReadable(EventLoop& loop, RequestHandler& handler) {
        char buf[4096];
        ssize_t n = recv(_fd, buf, sizeof(buf), 0);
        if (n == 0) { _state = CLOSING; return; }    // 相手が切断
        if (n < 0)  { return; }                       // 今は無い。次のpollへ
        _lastActive = time(NULL);
        _req.consume(std::string(buf, n));            // 逐次パーサへ供給

        if (_req.hasError()) { /* 400等を組んで WRITING へ */ return; }
        if (!_req.isComplete()) return;               // まだ途中。次のpollを待つ

        // ---- 完成: 第13章の結合コード(NULLチェック込み)をここに ----
    }
    // POLLOUT → ここが呼ばれる
    void onWritable(EventLoop& loop) {
        if (_outBuf.empty()) { loop.modifyEvents(_fd, POLLIN); return; }
        ssize_t n = send(_fd, _outBuf.data(), _outBuf.size(), 0);
        if (n < 0) return;                            // 今は書けない
        _lastActive = time(NULL);
        _outBuf.erase(0, static_cast<size_t>(n));     // ★送れた分だけ削る
        if (_outBuf.empty()) {
            if (keepAlive()) { _req.reset(); _state = READING;
                               loop.modifyEvents(_fd, POLLIN); }
            else             { _state = CLOSING; }
        }
    }
    void onCgiDone(EventLoop& loop, const HttpResponse& r) {
        _outBuf = r.serialize();
        _state  = WRITING;
        loop.modifyEvents(_fd, POLLOUT);
    }
    bool isTimedOut(time_t now) const { return now - _lastActive > 60; }
};
```

### 15.4 EventLoop — 唯一の poll を持つ中心

責務: (1) pollfd 配列の管理(登録/変更/解除)、(2) poll 実行、(3) イベントを持ち主(listener/conn/cgi)へディスパッチ、(4) タイムアウト掃除、(5) Connection/CgiProcess の生成・破棄の**所有者**になる(delete 責任の一元化; 第7章)。

```cpp
class EventLoop {
    std::vector<struct pollfd>    _pfds;
    std::map<int, ListenSocket*>  _listeners;
    std::map<int, Connection*>    _conns;
    std::map<int, CgiProcess*>    _cgis;     // CGIパイプfd → CgiProcess
public:
    void run() {
        while (g_running) {
            int ready = poll(&_pfds[0], _pfds.size(), 1000);  // ★1秒タイムアウト
            if (ready < 0) continue;   // シグナル割り込み等。落とさない
            dispatchEvents();          // reventsの立ったfdを処理
            sweepTimeouts();           // ★毎周、時間切れの接続とCGIを掃除
        }
    }
    void modifyEvents(int fd, short ev);   // POLLIN/POLLOUTの付け替え
    void attachCgi(int pipeFd, CgiProcess* p);   // 境界面#2
    void detachCgi(int pipeFd);
    // ...
};
```

実装上の注意:

- **poll のタイムアウトは有限値(例: 1000ms)にする。** -1(無限待ち)だと、イベントが来ない限りタイムアウト掃除が回らず、「無言の接続を切る」処理が実行されない。
- **ディスパッチ中の配列変更に注意。** close で pfds から要素を消すとインデックスがずれる。「消すべき fd を一旦リストに溜め、走査後にまとめて消す」方式が安全。
- POLLERR / POLLHUP / POLLNVAL が revents に立っていたら、その接続は CLOSING へ。
- シグナル: SIGINT で g_running を false にして綺麗に終了(全 fd close)。**SIGPIPE は無視設定(signal(SIGPIPE, SIG_IGN))にする**こと——切断済みソケットへの send でプロセスごと殺されるのを防ぐ定番の罠。

### 15.5 タイムアウト掃除(ハング防止の実装)

「リクエストが永遠にハングしない」要件はここで守る。

```cpp
void EventLoop::sweepTimeouts() {
    time_t now = time(NULL);
    // (1) 無言のクライアント → 408を返すか即close
    //     _conns を走査し isTimedOut(now) のものを処理
    // (2) 長すぎる CGI → killProcess() + 504(第18章)
    //     _cgis を走査し isTimedOut(now) のものを処理
}
```

### ✅ この章の要点

- echo サーバーを3クラスへ整理 → 複数ポート → 状態機械 → スタブ結合、の順で常に動くものを保つ。
- fd を得たら即 O_NONBLOCK。送受信は「一部だけ進む」前提でバッファを削る。
- poll のタイムアウトは有限値にして毎周タイムアウト掃除。SIGPIPE は無視設定。
- Connection/CgiProcess の delete 責任は EventLoop に一元化。

---

## 第16章 HTTPパーサの実装(逐次パース)

担当Bの最初の山。**「データが途中までしか無い」状態を常に想定する**のが同期的パーサとの違い。

### 16.1 逐次パースの構え

consume(bytes) は「受け取った分を内部バッファに足し、**進めるところまで状態を進めて、途中でも平然と戻る**」関数である。

```
状態: PARSE_REQUEST_LINE → PARSE_HEADERS → PARSE_BODY / PARSE_CHUNKED
                                             → PARSE_COMPLETE / PARSE_ERROR
```

```cpp
void HttpRequest::consume(const std::string& bytes) {
    _raw += bytes;                       // 溜める
    while (true) {
        if (_state == PARSE_REQUEST_LINE) {
            size_t pos = _raw.find("\r\n");
            if (pos == std::string::npos) return;   // ★行が揃ってない→次を待つ
            parseRequestLine(_raw.substr(0, pos));  // 失敗なら _state=PARSE_ERROR
            _raw.erase(0, pos + 2);
            _state = PARSE_HEADERS;
        }
        else if (_state == PARSE_HEADERS) {
            size_t pos = _raw.find("\r\n");
            if (pos == std::string::npos) return;
            std::string line = _raw.substr(0, pos);
            _raw.erase(0, pos + 2);
            if (line.empty()) {                      // 空行=ヘッダ終了
                decideBodyMode();                    // CL / chunked / なし
                continue;
            }
            parseHeaderLine(line);                   // "Name: value" を格納
        }
        else if (_state == PARSE_BODY) {
            if (_raw.size() < _contentLength) return; // ★まだ足りない→待つ
            _body = _raw.substr(0, _contentLength);
            _state = PARSE_COMPLETE; return;
        }
        else if (_state == PARSE_CHUNKED) {
            if (!consumeChunks()) return;             // 16.3節
        }
        else return;   // COMPLETE / ERROR
    }
}
```

ポイント:

- 「必要な区切り(\r\n や必要バイト数)が揃っていなければ、**黙って return**」——これが逐次パースの呼吸。次の recv 後にまた consume が呼ばれ、続きから進む。
- ヘッダ名は**小文字に正規化して格納**すると比較が楽(Host も host も同じキーに)。
- リクエストラインの検証: メソッドが GET/POST/DELETE 以外 → 501。書式不正 → 400。HTTP/1.1 で Host 欠落 → 400。

### 16.2 ボディ方式の決定と上限チェック

ヘッダ完了時(decideBodyMode)に:

1. `Transfer-Encoding: chunked` があれば → PARSE_CHUNKED へ。
2. `Content-Length: N` があれば → N を記録して PARSE_BODY へ。**このとき N が client_max_body_size を超えていたら、読む前に 413 を確定**させる(巨大ボディを受けきってから断るのはメモリの無駄+攻撃耐性の穴)。
3. どちらも無ければボディ無し → PARSE_COMPLETE。

### 16.3 chunked デコードの実装

チャンクは「16進サイズ行 → データ → \r\n」の繰り返しで、サイズ0が終端(8.3節)。逐次で書くには、チャンク内の進行状態も持つ。

```cpp
// 戻り値: true=状態が進んだ(ループ継続) / false=データ不足(returnして次を待つ)
bool HttpRequest::consumeChunks() {
    if (_chunkRemain == 0) {                       // 次のサイズ行を読む番
        size_t pos = _raw.find("\r\n");
        if (pos == std::string::npos) return false;
        _chunkRemain = strtoulHex(_raw.substr(0, pos)); // 16進→数値
        _raw.erase(0, pos + 2);
        if (_chunkRemain == 0) {                   // サイズ0=終端
            _state = PARSE_COMPLETE;               // (厳密には後続trailerの\r\n消費)
            return true;
        }
        checkBodyLimit();                          // 累計が上限超過なら413へ
    }
    size_t take = std::min(_chunkRemain, _raw.size());
    _body.append(_raw, 0, take);                   // ★un-chunk済みボディに蓄積
    _raw.erase(0, take);
    _chunkRemain -= take;
    if (_chunkRemain == 0) {                       // チャンク末尾の\r\nを消費
        if (_raw.size() < 2) { _chunkRemain = 0; /* 待ち方の工夫が必要 */ }
        else _raw.erase(0, 2);
    }
    return !_raw.empty();
}
```

(チャンク末尾 \r\n の待ち合わせなど細部は状態変数をひとつ足して整える。「サイズ行待ち/データ待ち/末尾CRLF待ち」の3サブ状態で考えると綺麗に書ける。)

**un-chunk 済みの _body だけを外に見せる**こと。CGI へは chunked のまま渡してはならない(課題要件)。

### 16.4 パーサは単体テストできる(基盤なしで開発可能)

パーサはソケット不要で、文字列を食わせるだけでテストできる。担当Aの基盤を待たずに進められる理由がこれ。

```cpp
// テストの発想: リクエストを意地悪に分割して食わせても同じ結果になるか
HttpRequest r;
r.consume("GET /index.h");         // 途中で切る
r.consume("tml HTTP/1.1\r\nHo");   // ヘッダ途中で切る
r.consume("st: x\r\n\r\n");
assert(r.isComplete() && r.path() == "/index.html");
```

**1バイトずつ食わせるテスト**は逐次パーサの品質を一発で暴く。必ずやること。

### ✅ この章の要点

- 逐次パースの呼吸 =「区切りが揃わなければ黙って return、次の consume で続きから」。
- ヘッダ名は小文字正規化。Content-Length は読む前に上限チェック(413)。
- chunked は3サブ状態(サイズ行/データ/末尾CRLF)で逐次デコードし、un-chunk 済みを _body に。
- パーサは文字列単体テストで鍛える。1バイト刻みテストが最強の検査。

---

## 第17章 RequestHandler — メソッド処理と静的配信

### 17.1 処理の全体順序(前段チェック → メソッド分岐)

handle() の中の判定順序を固定する。**順序を間違えると誤ったステータスを返す**。

```
① redirect 指定あり? → 301/302 を返して終了
② メソッドが allowedMethods に無い? → 405 (+ Allowヘッダ必須)
③ パス解決 + パストラバーサル検査 → 外に出るなら 403
④ 拡張子が cgi 対象? → CgiProcess::start → CGI_STARTED を返す
⑤ メソッド別処理 (GET / POST / DELETE)
```

### 17.2 パス解決とパストラバーサル防止(重大なセキュリティ要件)

URL のパスを実ファイルパスに変換する。location の root への付け替え:

```
location /kapouet の root が /tmp/www のとき
  URL /kapouet/pouic/toto/pouet → /tmp/www/pouic/toto/pouet
  (location の path 部分を root に置き換える)
```

**危険**: `GET /../../etc/passwd` のようなパスを素朴に連結すると root の外のファイルが読める(パストラバーサル攻撃)。評価者はこの種のパスを必ず試す。

対策の実装:

```cpp
// 方針: パスを "/" で分割し、"." は無視、".." は一段戻る。
// 戻りすぎ(rootより上に出る)を検出したら false(=403)。
bool resolvePath(const std::string& root, const std::string& urlPath,
                 std::string& out) {
    std::vector<std::string> stack;
    // urlPath を '/' で分割して走査
    //   ".." が来たら: stackが空なら false(外に出た)。あれば pop_back。
    //   "." と空要素は無視。それ以外は push_back。
    // out = root + "/" + join(stack, "/")
    // (URLデコード %2e%2e → .. も先に解いてから検査すること)
    ...
}
```

URL エンコード(%2e = '.')でごまかす手口もあるため、**先に %XX をデコードしてから**検査する。全メソッド(GET/POST/DELETE)のパス解決で共通に通す。

### 17.3 GET — 静的配信・index・autoindex

```
解決したパスが…
 ├─ ファイル      → open/read/close(★poll不要:ディスク例外)して 200
 │                  Content-Type を拡張子から設定(.html→text/html, .png→image/png...)
 ├─ ディレクトリ  → index ファイル(例 index.html)が中にある? 
 │      ├─ ある   → それを 200 で返す
 │      └─ ない   → autoindex on  → 一覧HTMLを生成して 200
 │                  autoindex off → 403
 └─ 存在しない    → 404
```

autoindex(ディレクトリ一覧)の実装は opendir / readdir / closedir(許可関数)で:

```cpp
DIR* d = opendir(fsPath.c_str());
// readdir をループしてエントリ名を集め、
// <a href="名前">名前</a> を並べた HTML を組み立てる。closedir を忘れずに。
```

ファイル種別の判定は stat(許可関数)で: S_ISDIR(st.st_mode) がディレクトリ判定。

### 17.4 POST — アップロード

1. location に uploadStore が無ければ、その location でアップロード不可(403 など)。
2. Content-Type が multipart/form-data なら boundary で分割し(8.4節)、filename とデータを取り出す。
3. uploadStore のディレクトリにファイルとして書き出す(open/write/close: ディスクなので poll 不要)。
4. 成功したら 201 Created(Location ヘッダに保存先URLを入れると丁寧)。

シンプル志向の許容: multipart でない生ボディ POST は「ボディをそのままファイル保存」でもデモは成立する。ただし multipart は実ブラウザのフォームが送る形式なので、ブラウザデモには対応が要る。

### 17.5 DELETE — HTML からの呼び出しも含めて

サーバー側: パス解決 → 存在確認 → ファイルなら削除(std::remove または unlink 相当; access/stat は許可関数)→ 204。無ければ 404。ディレクトリや権限不可は 403。

クライアント側の補足: HTML の form は GET/POST しか送れないため、**DELETE のデモには数行の JavaScript が要る**(これはデモ資材の話で、C++実装ではない):

```html
<button onclick="fetch('/upload/test.txt',{method:'DELETE'})
                 .then(r=>alert(r.status))">削除</button>
```

### 17.6 HttpResponse の serialize

```cpp
std::string HttpResponse::serialize() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << _status << " " << reasonPhrase(_status) << "\r\n";
    for (std::map<std::string,std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it)
        oss << it->first << ": " << it->second << "\r\n";
    oss << "\r\n";
    return oss.str() + _body;    // ボディはバイナリ可。<<で混ぜない
}
```

setBody で Content-Length を自動設定し、整合を機械的に保証する。

### ✅ この章の要点

- 判定順序を固定: redirect → 405(+Allow) → パス検査(403) → CGI → メソッド処理。
- パストラバーサルは「デコード→ .. をスタックで解決→ root 外なら403」。全メソッド共通。
- GET はファイル/ディレクトリ/不在で分岐。autoindex は opendir/readdir で生成。
- POST は multipart を boundary で分割して保存(201)。DELETE のデモには fetch の数行が要る。


---

## 第18章 CGI 完全ガイド — 唯一の非同期処理

CGI はこの課題の最難関である。fork / execve / pipe / dup2 という新しい道具が一気に登場し、しかも poll との非同期統合が要る。この章で一つずつ解体する。

### 18.1 CGI とは何か(仕組みの本質)

CGI (Common Gateway Interface) は「**サーバーが外部プログラムを起動し、リクエスト情報を渡して、プログラムの出力をレスポンスとして返す**」古典的な仕組みである。

```
 ブラウザ → webserv → (環境変数+stdin で情報を渡す) → python script.py
                    ← (stdout に HTML を出力する)    ←
```

CGI スクリプト側の視点(例: Python):

- リクエストの情報は**環境変数**(REQUEST_METHOD, QUERY_STRING...)で受け取る
- POST のボディは**標準入力(stdin)** から読む
- レスポンスは**標準出力(stdout)** に書く(ヘッダ数行 + 空行 + ボディ)

つまり webserv 側の仕事は「環境変数を用意し、stdin/stdout を自分と繋いだ状態で、スクリプトを別プロセスとして起動する」ことである。

### 18.2 道具その1: fork — プロセスの分身

fork() は**呼び出したプロセスの完全なコピー(子プロセス)を作る**システムコール。呼んだ瞬間からプログラムが2つ並行して動く。

```cpp
pid_t pid = fork();
if (pid < 0)  { /* 失敗 */ }
else if (pid == 0) { /* ★ここは子プロセスだけが通る */ }
else          { /* ★ここは親(webserv本体)。pid=子のプロセスID */ }
```

「1回呼ぶと2回返る」— 子では 0 が、親では子の PID が返る。この戻り値で自分がどちらかを知る。

**課題ルール: fork は CGI のためだけ。** 「接続ごとに fork して並行処理」のような設計は禁止(並行処理は単一 poll でやる)。

### 18.3 道具その2: pipe — プロセス間の一方通行の土管

pipe() は「書き込み口」と「読み出し口」のペア(fd 2つ)を作る。片方に書いたバイトが、もう片方から読める。

```cpp
int fds[2];
pipe(fds);   // fds[0]=読み出し口, fds[1]=書き込み口
```

CGI では**2本**使う: webserv→CGI(POSTボディを渡す)と CGI→webserv(出力を受け取る)。

```
              inPipe                     outPipe
 webserv ──[書]────[読]── CGI(stdin)  CGI(stdout) ──[書]────[読]── webserv
          inFd(親が書く)                            outFd(親が読む)
```

### 18.4 道具その3: dup2 — fd の付け替え

dup2(a, b) は「fd b を、a と同じものを指すように付け替える」。子プロセスで**パイプを stdin/stdout に成りすまさせる**ために使う。

```cpp
dup2(inPipe[0],  STDIN_FILENO);   // 子のstdin(0番) = inPipeの読み口
dup2(outPipe[1], STDOUT_FILENO);  // 子のstdout(1番) = outPipeの書き口
```

これで、スクリプトが「普通に標準入力を読む」と親が書いたボディが届き、「普通に print する」と親のパイプに流れる。スクリプト側は自分が CGI として動いていることを、環境変数以外意識しない。

### 18.5 道具その4: execve — 自分を別プログラムに置き換える

execve は「現在のプロセスを、指定したプログラムで**上書き**する」。fork した子で呼ぶことで、「分身がスクリプト実行機に変身する」。

```cpp
char* argv[] = { (char*)"/usr/bin/python3", (char*)scriptPath.c_str(), NULL };
char* envp[] = { /* CGI環境変数の配列(18.6節) */, NULL };
execve("/usr/bin/python3", argv, envp);
// execveが成功したらここには戻らない。戻ってきたら失敗 → 子はexitする
std::exit(1);
```

### 18.6 CGI 環境変数 — リクエスト情報の受け渡し

課題は「リクエストと引数の情報が CGI から見えること」を要求する。最低限用意する変数:

| 変数 | 内容 | 例 |
|---|---|---|
| REQUEST_METHOD | メソッド | GET / POST |
| QUERY_STRING | ? 以降のクエリ | name=foo&x=1 |
| CONTENT_LENGTH | ボディ長(un-chunk後) | 42 |
| CONTENT_TYPE | ボディの型 | multipart/form-data; ... |
| PATH_INFO | スクリプト後の追加パス | /extra/path |
| SCRIPT_FILENAME / SCRIPT_NAME | スクリプトの実パス/URL | ./cgi-bin/app.py |
| SERVER_PROTOCOL | HTTP/1.1 | |
| SERVER_NAME / SERVER_PORT | ホスト/ポート | localhost / 8080 |
| GATEWAY_INTERFACE | CGI/1.1 | |

C++98 での組み立て: `std::vector<std::string>` に "KEY=value" を積み、execve 直前に `char*` の配列へ変換(各 .c_str() を並べ、末尾 NULL)。

### 18.7 start() の全手順(親と子の分岐)

```cpp
bool CgiProcess::start(const HttpRequest& req, const LocationConfig& loc,
                       Connection& owner) {
    int inP[2], outP[2];
    if (pipe(inP) < 0 || pipe(outP) < 0) return false;

    _pid = fork();
    if (_pid < 0) return false;

    if (_pid == 0) {                       // ===== 子プロセス =====
        dup2(inP[0],  STDIN_FILENO);       // stdin ← 親からのボディ
        dup2(outP[1], STDOUT_FILENO);      // stdout → 親へ
        close(inP[0]); close(inP[1]);      // dup済みなので元は全部閉じる
        close(outP[0]); close(outP[1]);
        chdir(scriptDir.c_str());          // ★スクリプトのディレクトリで実行
                                           //  (相対パス参照を正しくする課題要件)
        execve(interp, argv, envp);
        std::exit(1);                      // execve失敗時のみ到達
    }
    // ===== 親(webserv) =====
    close(inP[0]);  close(outP[1]);        // 親は使わない側を閉じる(重要!)
    _inFd  = inP[1];                       // 親が書く側
    _outFd = outP[0];                      // 親が読む側
    fcntl(_inFd,  F_SETFL, O_NONBLOCK);    // ★パイプもノンブロッキング
    fcntl(_outFd, F_SETFL, O_NONBLOCK);
    _inBuf = req.body();                   // un-chunk済みボディを送る準備
    _owner = &owner;
    _startTime = time(NULL);
    _state = _inBuf.empty() ? CGI_READING_OUTPUT : CGI_WRITING_INPUT;
    if (_inBuf.empty()) { close(_inFd); _inFd = -1; }  // ボディ無し→即EOF通知
    // ★境界面#2: パイプをpollへ
    //   loop.attachCgi(_inFd, this)  (書く側; あれば)
    //   loop.attachCgi(_outFd, this) (読む側)
    return true;
}
```

**「使わない側の close」を忘れると詰む。** 特に親が inP[1](書き口)を持ったまま子側のコピーも残っていると、CGI の stdin に EOF が届かず、スクリプトが入力を待ち続けてハングする。パイプの fd 管理は「dup2 後、不要な口はすべて閉じる」を機械的に守る。

### 18.8 パイプの非同期 I/O(poll イベントで進める)

パイプは「待たされる fd」なので、poll の通知でしか読み書きしない(0点ルールの対象!)。

```cpp
void CgiProcess::onWritable() {           // inFd の POLLOUT で呼ばれる
    ssize_t n = write(_inFd, _inBuf.data(), _inBuf.size());
    if (n < 0) return;                    // 今は書けない(errnoは見ない)
    _inBuf.erase(0, n);
    if (_inBuf.empty()) {                 // 書き切った
        // pollから外して close = CGIに「本文はここまで(EOF)」を伝える
        close(_inFd); _inFd = -1;
        _state = CGI_READING_OUTPUT;
    }
}
void CgiProcess::onReadable() {           // outFd の POLLIN で呼ばれる
    char buf[4096];
    ssize_t n = read(_outFd, buf, sizeof(buf));
    if (n < 0) return;
    if (n == 0) {                         // EOF = CGIが出力を終えた
        _state = CGI_DONE;
        _owner->onCgiDone(buildResponse());   // 境界面#3
        // waitpid(WNOHANG)で回収、パイプをpollから外してclose
        return;
    }
    _outBuf.append(buf, n);
}
```

**CGI 出力の終端は EOF。** 課題が明記する通り、CGI が Content-Length を返さなければ「read が 0 を返す = 出力終了」とみなす。

### 18.9 CGI 出力 → HttpResponse への変換

CGI の stdout は「簡易ヘッダ + 空行 + ボディ」の形で出てくる:

```
Content-Type: text/html\r\n     (または \n のみの行儀の悪いスクリプトもある)
Status: 200 OK\r\n              (省略されることが多い)
\r\n
<html>...
```

buildResponse の手順: (1) 最初の空行(\r\n\r\n、寛容にするなら \n\n も)でヘッダ部とボディを分割 → (2) Status ヘッダがあればそのコード、無ければ 200 → (3) Content-Type 等を写す → (4) ボディをセット(Content-Length は自動計算)。ヘッダらしき部分が無ければ全体をボディとして 200 で包む、と寛容にしておくとデモ用スクリプトが楽。

### 18.10 waitpid と WNOHANG — 子の回収でブロックしない

終了した子プロセスは waitpid で「回収」しないとゾンビとして残る。ただし:

```cpp
// ✗ waitpid(_pid, &st, 0)      … 子が終わるまでブロックする危険
// ✓ WNOHANGで「終わってなければ待たずに0を返す」
int r = waitpid(_pid, &status, WNOHANG);
if (r == 0)      { /* まだ生きてる → 次のループで再試行 */ }
else if (r == _pid) { /* 回収完了 */ }
```

「出力 EOF を見た直後でも、プロセスが完全終了する前の一瞬」がありうる。そこでブロッキング版を呼ぶとサーバー全体が固まりかねない。**WNOHANG + 次ループで再試行**がノンブロッキング方針と整合する作法。

### 18.11 CGI タイムアウト — 無限ループするスクリプト対策

スクリプトが無限ループしたら、EOF は永遠に来ない。放置は「リクエストが永遠にハングしない」要件への違反。対策:

1. start 時に _startTime を記録。
2. EventLoop の毎周の掃除(15.5節)で、一定時間(5〜10秒)超過の CGI を検出。
3. `kill(_pid, SIGKILL)` で強制終了(kill は許可関数)→ waitpid(WNOHANG は直後なら確実に回収できる)→ パイプを poll から外して close。
4. クライアントには **504 Gateway Timeout** を onCgiDone 経由で返す。

評価では「無限ループする CGI を食わせてもサーバーが生きているか」を試されることがある。`while True: pass` の Python を cgi-bin に置いて自分で確認しておくこと。

### ✅ この章の要点

- CGI = fork(分身)→ dup2(パイプをstdin/stdoutに変装)→ execve(変身)。情報は環境変数+stdin、結果はstdout。
- パイプは2本。親は使わない口を必ず閉じる(閉じ忘れ=EOFが届かずハング)。
- パイプはノンブロッキング化して poll 管理下に。書き切ったら close で EOF 通知。出力終端は read==0。
- waitpid は WNOHANG。CGI にはタイムアウト(SIGKILL + 504)を必ず付ける。

---

## 第19章 堅牢性 — クラッシュしないサーバーにする

「どんな状況でもクラッシュ・異常終了しない」(違反=0点)を体系的に守る。

### 19.1 クラッシュの主要因と対策一覧

| 原因 | 対策 |
|---|---|
| NULL ポインタのデリファレンス | Router::match の NULL チェック(13.3節)ほか、ポインタは使用前に必ず確認 |
| 送信先が切断済みのソケットに send → SIGPIPE でプロセス死 | 起動時に `signal(SIGPIPE, SIG_IGN)`(15.4節)。以後 send は -1 を返すだけになる |
| メモリ枯渇(new が bad_alloc を投げる) | main 最上位の try/catch(7.5節)。可能なら該当接続だけ閉じて継続 |
| 巨大ボディでメモリ爆発 | Content-Length を読む前に 413 判定(16.2節)。chunked も累計で監視 |
| fd 枯渇 | close 徹底(2.3節)。接続・CGIパイプ・開いたファイル、全てにcloseの対を |
| 不正リクエストでパーサが暴走 | パーサの全分岐で PARSE_ERROR に落とせる構え。1バイト刻みテスト(16.4節) |
| 設定ミスで稼働中に破綻 | 検証は起動時に全部(14.3節)。起動後の Config は不変 |
| ゾンビプロセス蓄積 | waitpid(WNOHANG) の徹底(18.10節) |

### 19.2 タイムアウトの三層防御

「永遠にハングしない」を守る時限装置は3つある。全部入れる。

1. **poll のタイムアウト(1秒程度)** — イベントが無くても定期的にループが回り、下2つの掃除が実行される土台。
2. **接続タイムアウト** — 無言のクライアントを一定時間で 408 or 切断。
3. **CGI タイムアウト** — 返事しないスクリプトを SIGKILL して 504。

### 19.3 リークの検査

- 長時間・大量アクセス後に fd 数を確認: Mac なら `lsof -p <pid> | wc -l` が増え続けないこと。
- メモリ: 同条件で leaks(Mac)/ valgrind(Linux)を実行。「接続のたびに増える」パターンが最悪(new/close の対応漏れ)。

### ✅ この章の要点

- SIGPIPE 無視・最上位 try/catch・NULL チェック・413 の事前判定、この4点は初日から入れる。
- タイムアウトは三層(poll有限値/接続/CGI)。
- lsof と leaks/valgrind で「増え続けないこと」を確認する習慣を。

---
---

# 第V部 仕上げ編

---

## 第20章 テスト戦略

### 20.1 テストの道具箱

| 道具 | 何を確かめるか |
|---|---|
| ブラウザ(Chrome等) | 実環境互換(課題要件)。静的表示・フォームアップロード・CGI |
| curl | メソッド/ヘッダを自在に: `curl -X DELETE`, `curl -F "file=@x.png"`(multipart), `curl -H "Transfer-Encoding: chunked"` |
| nc / telnet | **生のHTTPを手打ち**。不正・部分リクエスト耐性の確認に最強 |
| 自作スクリプト(Python等) | 並列接続・遅いクライアント・自動回帰。課題も「複数言語でテストせよ」と要求 |
| NGINX | 同じリクエストへの応答ヘッダを比較して答え合わせ |
| siege / ab など | ストレステスト(可用性の確認) |

### 20.2 必ずやるべきテストシナリオ

**正常系**: 静的GET(HTML/画像/CSS)/ディレクトリ→index/autoindex一覧/アップロード→保存確認/DELETE→消えたか/CGI GET・POST/複数ポートで違うコンテンツ/リダイレクト。

**異常系(評価者が突く場所)**:

```
・nc で途中まで送って放置        → タイムアウトで切られるか。サーバーは生きてるか
・壊れたリクエストライン         → 400 が返るか(クラッシュしないか)
・GET /../../etc/passwd          → 403 か(パストラバーサル)
・許可外メソッド                 → 405 + Allow ヘッダ
・client_max_body_size 超過      → 413(受け切る前に)
・無限ループCGI                  → 504、サーバー継続
・存在しないパス                 → 404(自作エラーページ)
・keep-alive で連続リクエスト     → 2発目も正しく処理されるか
・同時接続 100+                  → 全部に応答が返るか、fd/メモリが漏れないか
・巨大ファイルのGET              → 部分送信が正しく続きを送るか
```

### 20.3 遅いクライアントのシミュレート

ノンブロッキング実装の真価は「遅い相手に引きずられない」こと。1バイトずつゆっくり送るクライアントを Python で書き、**その間も別の接続が普通に処理される**ことを確認する。これが通れば基盤は本物である。

```python
import socket, time
s = socket.create_connection(("localhost", 8080))
for ch in "GET / HTTP/1.1\r\nHost: x\r\n\r\n":
    s.send(ch.encode()); time.sleep(0.2)   # 1文字ずつ0.2秒間隔
print(s.recv(4096))
```

### ✅ この章の要点

- ブラウザ+curl+nc+自作スクリプトを併用(単一ツール依存は課題も禁じる)。
- 異常系リストを潰す。特にパストラバーサル・無限CGI・部分リクエストは評価頻出。
- 「遅いクライアント中でも他が動く」テストが基盤の卒業試験。

---

## 第21章 評価(defense)対策

### 21.1 評価で起きること

- 評価者(ピア)が課題シートに沿って機能を1つずつ実演させる。conf を見せ、各機能を動かす。
- **コードについての説明を求められる**。「なぜ poll なのか」「errno を見ていない理由は」「この fd はどこで close されるか」。
- **その場での小さな修正**を求められることがある(数分で終わる変更; 表示の変更やデータ構造の調整など)。理解していないコードだとここで詰む。

### 21.2 説明できるべき頻出質問(想定問答)

1. **なぜノンブロッキング+単一pollなのか?** → ブロッキングは1クライアントに全体が道連れ。pollで多重監視+ノンブロッキングで万一も止まらない(第4〜5章)。
2. **read/write後にerrnoを見ない理由と、代わりの判断材料は?** → errnoは残留・上書きで信頼できない。戻り値(0=切断/正=進んだ/負=次のpoll待ち)+poll通知だけで足りる(第6章)。
3. **ディスクファイルにpollが不要な理由は?** → 相手待ちが原理的に無い。pollは常に「読める」としか答えない(第2・6章)。
4. **CGIはどう動く? forkの後それぞれ何をする?** → 子: dup2でパイプをstdin/stdoutに→chdir→execve。親: 不要口close→ノンブロ化→pollに載せる(第18章)。
5. **keep-aliveはどう実装した?** → 送信完了後、Connection: closeが無ければreqをリセットしREADINGへ(第9・15章)。
6. **無限ループするCGIが来たら?** → 起動時刻を記録、掃除でSIGKILL+waitpid、クライアントに504(第18章)。
7. **同じfdを2回closeしたら何が起きる? どう防いでいる?** → 二重closeは別の接続のfdを誤爆しうる。close後は-1を代入し、所有者を一元化(第7・15章)。
8. **POLLOUTを常時立てない理由は?** → pollが即返し続けCPU空回り。送信待ちがある間だけ立てる(第5章)。

### 21.3 チームとしての備え

- **自分が書いていない側のコードを読み合う時間を週に確保する。** 「相方担当だから分からない」は通らない。
- README を課題要件どおりに: 1行目の定型文(斜体・ログイン名)、Description / Instructions / Resources、**AIをどのタスク・どの部分で使ったかの明記**、英語。
- デモは自分たちで一度通しリハーサルする: conf → 起動 → 各機能を見せる順番を決めておく。

### ✅ この章の要点

- 評価は「動くこと」+「説明できること」+「その場で直せること」。
- 想定問答21.2を自分の言葉で答えられるようにしておく(本書の該当章がそのまま答えになる)。
- README の AI 使用記述と定型文を忘れない。

---

## 第22章 1ヶ月ロードマップ

残り1ヶ月・2人(あなた=時間多め・担当A基盤+遊撃 / 相方=担当Bメイン)前提の計画。

### Week 0(最初の2日)

- [ ] 2人で webserv.hpp(境界面)を読み合わせ、4点(handle/attachCgi/onCgiDone/findServer・match)に合意
- [ ] リポジトリ整備: ディレクトリ構成、Makefile、.gitignore、docs/ に設計書
- [ ] 本書 第I部を両者が読了(概念の共通言語を作る)

### Week 1 — 土台

- あなた: echoサーバー→3クラス分割→複数ポート→ConnState化→**スタブhandleでブラウザにHello**(第15章)。SIGPIPE無視・try/catchも初日に。
- 相方: HttpRequest逐次パーサ(リクエストライン+ヘッダ)を**文字列単体テスト**で(第16章)。1バイト刻みテスト必須。
- 合流: あなたの基盤+相方のパーサを繋ぎ、「本物のGETをパースして固定レスポンス」まで。

### Week 2 — 静的サーバー完成

- 相方: Router::match(NULL処理)、パス解決+**パストラバーサル防止**、GET静的配信、404/403、HttpResponse(第17章)
- あなた: 設定パーサ(第14章)→ハードコード撤去。接続タイムアウト掃除。エラーページ二段構え。
- 合流: **conf で動く静的サイト**。nc で異常系を叩き始める。

### Week 3 — メソッド完成と CGI 前半

- 相方: POSTアップロード(multipart)、DELETE、autoindex、リダイレクト、405+Allow
- あなた: attachCgi/detachCgi の poll 統合、CGIタイムアウト掃除の枠組み(第15・18章)。Content-Length事前413。
- 合流: CgiProcess::start(fork/dup2/execve/環境変数)を2人で書く(**ここは共同作業推奨**: 評価で両者に説明責任がある接点)

### Week 4 — CGI完成・keep-alive・仕上げ

- 前半: CGI非同期I/O完成(POSTボディ書き込み→EOF、出力読み取り、504、WNOHANG)。chunked un-chunk を CGI 経路で検証。
- 中盤: keep-alive 有効化。ストレステスト・遅いクライアントテスト(第20章)。lsof/leaks でリーク確認。
- 後半: デモ資材(www/ の HTML、DELETE用のfetchボタン、cgi-bin のスクリプト、見せる用conf)、README、通しリハーサル、想定問答(21.2)の口頭練習。

### 進捗が遅れたときの優先順位(死守ライン)

1. **0点ルールの遵守**(readiness/errno/クラッシュ/fork制限)— これだけは何があっても
2. 静的GET+設定ファイル+複数ポート+正確なステータス
3. POST/DELETE/アップロード/エラーページ
4. CGI(1種類でよい)+chunked
5. keep-alive・autoindex の磨き込み

「全部を60%」より「上から順に100%」。評価は Mandatory の完全性が前提(不完全なら Bonus も見てもらえない構造は、Mandatory 内の完成度にも同じ思想で臨むのが安全)。

---

# 付録

## 付録A 許可関数チートシート(何に使うか)

| 関数群 | 用途 | 本書の章 |
|---|---|---|
| socket, bind, listen, accept | サーバーの儀式 | 3 |
| setsockopt, getsockname | SO_REUSEADDR など | 3 |
| htons, htonl, ntohs, ntohl | バイトオーダー変換 | 3 |
| recv, send | ソケットI/O(poll後のみ!) | 3,6 |
| poll (select, epoll系, kqueue系) | I/O多重化(1つだけ使う) | 5 |
| fcntl | O_NONBLOCK化(F_SETFL/O_NONBLOCK/FD_CLOEXECのみ) | 4 |
| read, write | パイプ(poll後のみ)/ディスク(例外で自由) | 6,18 |
| open, close | ファイル/fdの取得と返却 | 2,17 |
| stat, access | ファイル種別・存在・権限 | 17 |
| opendir, readdir, closedir | autoindex | 17 |
| fork, execve, pipe, dup, dup2 | CGI専用 | 18 |
| waitpid, kill, signal | 子の回収(WNOHANG)/CGI強制終了/SIGPIPE無視 | 15,18 |
| chdir | CGIをスクリプトのディレクトリで実行 | 18 |
| strerror, gai_strerror, errno | セットアップ系エラー表示のみ(read/write後の分岐は禁止) | 6 |
| getaddrinfo, freeaddrinfo | ホスト名解決(hostに名前を書く場合) | 3 |
| getprotobyname, socketpair, connect | 補助(必須ではない) | — |

## 付録B 0点ルール最終チェックリスト

- [ ] recv/send/read/write(ソケット・パイプ)は、必ず直前の poll の revents 確認を通っている
- [ ] read/write の後に errno を参照する分岐が1箇所もない(grep で errno を検索して確認)
- [ ] fork の呼び出しは CgiProcess::start の1箇所だけ
- [ ] execve の対象は CGI インタプリタのみ(別のwebサーバーではない)
- [ ] どんな入力でも落ちない: nc手打ち・1バイト刻み・巨大ボディ・無限CGIで確認済み
- [ ] SIGPIPE 無視設定がある / main 最上位に try/catch がある
- [ ] すべての fd に close の対がある(lsof で増加しないことを確認済み)

## 付録C 用語ミニ辞典

- **fd(ファイルディスクリプタ)**: OSが発行する入出力の整理番号。ソケットもパイプもファイルも同じ仕組み(第2章)。
- **ブロッキング**: 結果が返るまでプログラムが止まること(第4章)。
- **ノンブロッキング**: できなければ「今は無理」とすぐ返る性質。fcntlで設定(第4章)。
- **I/O多重化**: 複数fdをまとめて監視し、動きのあったものを教えてもらう仕組み。poll等(第5章)。
- **readiness**: pollによる「このfdは今読める/書ける」の通知。これ無しの読み書きは0点(第6章)。
- **逐次パース**: データが途中までしか無くても壊れず、来た分だけ進めるパース方式(第16章)。
- **keep-alive**: HTTP/1.1の接続維持。1本の接続で複数リクエストを処理(第9章)。
- **chunked**: ボディを小分けで送る転送方式。サーバーはun-chunkする義務(第8,16章)。
- **パストラバーサル**: ../ でroot外のファイルを読む攻撃。正規化+範囲検査で403(第17章)。
- **CGI**: 外部プログラムに環境変数+stdinで情報を渡しstdoutを受け取る仕組み(第18章)。
- **ゾンビプロセス**: 終了したがwaitpidで回収されていない子プロセス(第18章)。
- **fdリーク**: closeし忘れたfdが溜まること。枯渇するとaccept不能に(第2章)。

---

*本書は設計書(docs/design.md)・共有ヘッダ(webserv.hpp)と併用する。設計の「なぜ」は本書、境界面の「正確な形」はwebserv.hpp、進行管理は設計書第9章が担う。*
