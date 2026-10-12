# R AppMarket

[English](README.md) · [한국어](README.ko.md) · **日本語** · [Italiano](README.it.md) · [Français](README.fr.md) · [Español](README.es.md)

R AppMarketを使うと、Terminalで`pkgman`コマンドを打たなくても、ウィンドウ一つで登録された各リポジトリのすべてのアプリを探してインストールできます。

![R AppMarket](screenshot.png)

## インストール

**arm64**、**x86_64**、32ビット**x86**(gcc2ハイブリッド)に対応しています。

1. **Terminal**を開く: 画面右上の羽根アイコンのメニューをクリックし、**Applications > Terminal**を選びます。
2. 自分のアーキテクチャに合うリポジトリを追加してからインストールします(わからない場合は`ls /system/packages/haiku-*.hpkg`で確認できます):

   **arm64**または**x86_64**の場合:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64    # または .../x86_64
   pkgman refresh
   pkgman install rappmarket
   ```

   32ビット**x86**(gcc2ハイブリッド)の場合、パッケージ名は`rappmarket_x86`です:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2
   pkgman refresh
   pkgman install rappmarket_x86
   ```

3. Terminalで確認を求められたら、**y**と入力して**Enter**キーを押します。
4. 完了するとデスクトップに**R AppMarket**アイコンが表示されます。ダブルクリックするとアプリを探してインストールできます。

## ライセンス

MIT。

## AI利用について

このプログラムはClaudeを使って書かれました。
