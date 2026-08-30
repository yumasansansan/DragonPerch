<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# 実行のしかた

コマンドライン、トレイアイコン、診断モード。何を変えるかは[設定](settings.md)にあります。

英語版は [docs/running.md](../running.md) です。

## 動かしてみる

```bash
dragonperch --pets 6
dragonperch --pause
dragonperch --stop
```

Linux ではウィンドウの位置をコンポジタから受け取るので、先に KWin スクリプトを入れます:

```bash
./kwin/install.sh
```

`--pets N` は**各**マスコットを何体出すかで、設定ファイルの `pets-per-mascot` を上書きします。
既定は各1体なので、同梱の3体では3匹です。

## トレイアイコン

両プラットフォームにあり、DragonPerch を止める通常の方法です。右クリックで一時停止と終了。
Windows では `Shell_NotifyIcon`、Linux では StatusNotifierItem で、後者はメニューを**描くのではなく
記述します** — Plasma がラベルから Breeze で組み立て、ユーザのテーマに従います。こちら側にコードはありません。

Windows のメニューは本物の WinUI 3 の `MenuFlyout` で、`shell/windows/` にある別プログラム
`DragonPerch.Shell.exe` が描きます。別プロセスなのは測って決めたことです。XAML の初期化はプロセスに
恒久的に約50MB の専有メモリを要求し、閉じても返ってきません。だからツールキットは、必要になったときに
起動して、竜に気づかれずに殺せる場所に置いてあります。`dragonperch.exe` は App SDK と無縁の
2MB の Win32 プロセスのままです。

デーモンはポインタがトレイアイコンに乗った時点でシェルを起動します。冷えた WinUI プロセスがボタンを
押されるまでに必要な数百ミリ秒を、そこで稼ぎます。シェルが入っていない・起動しきっていない・
殺された場合は、デーモン自身の `TrackPopupMenuEx` のメニューを出します。待ちません。クリックの
0.5秒後に出るメニューはハングに見えるからです。**シェルは最も強い意味で任意です** — ディスク上に
影も形も無くてもデーモンは完全に使えます。

## 診断モード

```bash
dragonperch --dump-world --hold
dragonperch --probe-composition --hold
```

`--dump-world` は Linux で新しい機械に入れたとき最初に走らせるものです。KWin が言ってきた内容を表示し、
レンダラは一切起動しません。ウィンドウをドラッグして数字が追随するなら難しいほうの半分は動いており、
画面がまだおかしいならレンダラの問題です。

**診断モードは Debug 限定です。** `--probe-composition`・`--dump-world`・`--self-test`・
`--export-placeholder` はリリースビルドから消えます（パッケージと nightly はリリースです）。
無駄な重しではなく、このプロジェクトの難しいバグは全部どれかが見つけたものなので、スイッチは
構成とは別になっています:

```bash
cmake --preset windows-x64 -D DRAGONPERCH_DIAGNOSTICS=ON
```

これで診断入りの*リリース*バイナリが得られます。出荷したビルドがおかしいときに作るのはこれです。
Debug ビルドはタイミングも Direct2D の層も違うので、別の問いに答えてしまいます。

## 制御インターフェース

`--stop`・`--pause`・`--resume`・`--reload` は1つの制御インターフェースを通ります。Windows では
`WM_COPYDATA` に答えるメッセージ専用ウィンドウ、Linux ではセッションバスの `org.dragonperch.Control`。
どちらも専用スレッドを必要としません。Windows はオーバーレイのためにすでにメッセージを回しており、
Linux は KWin の報告が届くバスをすでに処理しています。トレイと設定プログラムは、同じ4つのコマンドの
別の呼び手にすぎません。

`--stop` があるのは、Windows では Ctrl+C が「ほぼ効く」からで、それは当てにできないからです。これは
GUI サブシステムのバイナリなのでシェルは待たず、プロンプトはすぐ戻ります。そこで打った Ctrl+C が
コンソールイベントになるかはシェル次第で、`cmd` は送りますが PowerShell 7 は送りません（PSReadLine が
キーを処理します）。コンソールを閉じるのも効きます。Linux では Ctrl+C が効きます。

一時停止はシミュレーションと描画を止めます。オーバーレイは最後のフレームのまま残ります。再開時に
作り直すとウィンドウとサーフェスのコードをもう一度通ることになり、そこはこのプログラムで最も
間違えてきた部分です。

## ログ

診断は標準出力**と**実行ファイルの隣の `dragonperch.log` に出ます。GUI サブシステムのバイナリは
コンソールがあることを当てにできないためです。
