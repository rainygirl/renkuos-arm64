# R AppMarket

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · [Italiano](README.it.md) · **Français** · [Español](README.es.md)

R AppMarket vous permet de parcourir et d'installer toutes les applications de chaque dépôt configuré -- dont les applications R* de RENKU -- depuis une fenêtre, sans avoir à taper de commandes `pkgman` dans le Terminal.

![R AppMarket](screenshot.png)

## Installation

Disponible pour **arm64**, **x86_64** et **x86** 32 bits (hybride gcc2).

1. Ouvrez **Terminal** : cliquez sur le menu de la plume en haut à droite de l'écran, puis **Applications > Terminal**.
2. Ajoutez le dépôt correspondant à votre architecture (vous ne savez pas laquelle ? `ls /system/packages/haiku-*.hpkg` vous le dit), puis installez :

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64      # arm64
   pkgman add-repo https://pkgman.rainygirl.com/x86_64     # x86 64 bits
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2   # x86 32 bits (hybride gcc2)

   pkgman refresh
   pkgman install rappmarket
   ```

3. Si Terminal pose une question, tapez **y** puis appuyez sur **Entrée**.
4. Une fois terminé, une icône **R AppMarket** apparaît sur le Bureau. Double-cliquez dessus pour parcourir et installer des applications.

## Licence

MIT.

## Mention sur l'usage de l'IA

Ce programme a été écrit avec Claude.
