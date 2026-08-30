<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# DragonPerch

**Konqi たちがウィンドウのタイトルバーやパネルの上を歩き回ります — Windows と Wayland のための、
クロスプラットフォームな XPenguins。**

マスコットがウィンドウの上端を歩き、タスクバーに腰かけ、下のウィンドウを動かすと落ちます。Konqi・Katie・Kori
が同梱されていますが、アートワークはスプライトパックなので、どんなマスコットでも歩かせられます。C++23、GPU 描画、
できるかぎりプラットフォームに近いところで。

> 状況: **Windows は動きます。** Konqi・Katie・Kori が KDE 自身のアートワークから GPU で描かれ、
> タイトルバーとタスクバーの上を歩きます。クリックは素通りし、全画面のアプリの邪魔をせず、Fluent の
> トレイメニューと設定ウィンドウがあります。
>
> **Linux も動きます。** Wayland 側は layer-shell のオーバーレイに EGL で描き、ウィンドウの位置は
> KWin スクリプトが教えます（Plasma 6 で確認済み）。トレイアイコンと KDE 設定モジュールがあります。
> この先の予定は[計画](../plan.md)にあります。

英語版は [README.md](../../README.md) です。

## 入手

`main` へのビルドごとに、更新され続ける **`nightly`** プレリリースが出ます。

| 環境 | やること |
|---|---|
| Debian / Ubuntu | `sudo apt install ./dragonperch_*.deb` |
| Plasma を使っている | 加えて `sudo apt install ./dragonperch-kde_*.deb` |
| その他の Linux | `.tar.gz` を好きな場所に展開して `usr/bin/dragonperch-wl` |
| Windows | `.zip` を展開して `dragonperch.exe` |

Linux では、マスコットがウィンドウを見つけられるように KWin スクリプトを一度導入します:

```bash
./kwin/install.sh
```

その後 **システム設定 → ウィンドウ管理 → KWin スクリプト** で有効にしてください。パッケージを入れただけでは
**有効になりませんし、ログイン時に起動もしません** — 依存関係で入ってきただけで人の画面にマスコットを出すプログラムは、
アンインストールされて当然です。

Windows Defender がダウンロードを検知することがあります。誤検知で、
[理由を書いたページ](packages.md#windows-defender-がダウンロードを検知する)があります。

## 使う

トレイアイコンを右クリック: **一時停止**・**設定**・**終了**。操作はそれだけです。

```bash
dragonperch --pets 6      # マスコットごとに6体
dragonperch --stop
```

コマンドラインの残りと診断モードは[実行のしかた](running.md)、設定ファイルと設定プログラムは
[設定](settings.md)にあります。

## 読む

| | |
|---|---|
| [実行のしかた](running.md) | コマンドライン、トレイ、診断、ログ |
| [設定](settings.md) | 設定ファイルと2つの設定プログラム |
| [パッケージ](packages.md) | 配布物、バージョンの並び方、Defender |
| [翻訳](translating.md) | 1つの文字列テーブルと、言語の追加方法 |

開発者向けの文書は英語のままです — [設計](../design.md)、[ビルド](../building.md)、
[fuzz ターゲット](../../fuzz/README.md)、[計画](../plan.md)。

## ライセンス

コードは `GPL-3.0-or-later` です。

アートワークは**違います**。Konqi・Katie・Kori は KDE コミュニティの作品で、**Tyson Tan** による
デザインと作画、Konqi のベクター化は **Franco Perez** によるもので、`CC-BY-SA-4.0` です。そのままに
してください。リンクされるのではなく実行時に読み込まれるデータのライセンスを変える理由はありません。
各パックは `artwork-licence` キーに自身の条件を、`AUTHORS.md` に作者を記しています。
