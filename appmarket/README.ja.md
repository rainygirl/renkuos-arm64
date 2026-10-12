# R AppMarket

[English](README.md) · [한국어](README.ko.md) · **日本語** · [Italiano](README.it.md) · [Français](README.fr.md) · [Español](README.es.md)

R AppMarketを使うと、Terminalで`pkgman`コマンドを打たなくても、ウィンドウ一つで登録された各リポジトリのすべてのアプリ(RENKUのR*アプリも含む)を探してインストールできます。

![R AppMarket](screenshot.png)

## インストール

**arm64**、**x86_64**、32ビット**x86**(gcc2ハイブリッド)に対応しています。

1. **Terminal**を開く: 画面右上の羽根アイコンのメニューをクリックし、**Applications > Terminal**を選びます。
2. 自分のアーキテクチャに合うリポジトリを追加します(わからない場合は`ls /system/packages/haiku-*.hpkg`で確認できます)。その後インストールします。

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64      # arm64
   pkgman add-repo https://pkgman.rainygirl.com/x86_64     # 64ビットx86
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2   # 32ビットx86 (gcc2ハイブリッド)

   pkgman refresh
   pkgman install rappmarket
   ```

3. Terminalで確認を求められたら、**y**と入力して**Enter**キーを押します。
4. 完了するとデスクトップに**R AppMarket**アイコンが表示されます。ダブルクリックするとアプリを探してインストールできます。

## ライセンス

MIT。

## AI利用について

このプログラムはClaudeを使って書かれました。
