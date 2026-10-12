# R AppMarket

**English** · [한국어](README.ko.md) · [日本語](README.ja.md) · [Italiano](README.it.md) · [Français](README.fr.md) · [Español](README.es.md)

R AppMarket lets you browse and install every app in each repository it knows about, from a window, instead of typing `pkgman` commands in Terminal.

![R AppMarket](screenshot.png)

## Install

Available for **arm64**, **x86_64**, and 32-bit **x86** (gcc2 hybrid).

1. Open **Terminal**: click the feather menu at the top right of the screen, then **Applications > Terminal**.
2. Add the repository for your architecture, then install (not sure which? `ls /system/packages/haiku-*.hpkg` tells you):

   On **arm64** or **x86_64**:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64    # or .../x86_64
   pkgman refresh
   pkgman install rappmarket
   ```

   On 32-bit **x86** (x86_gcc2 hybrid), the package is named `rappmarket_x86`:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2
   pkgman refresh
   pkgman install rappmarket_x86
   ```

3. If Terminal asks a question, type **y** and press **Enter**.
4. When it finishes, an **R AppMarket** icon appears on the Desktop. Double-click it to browse and install apps.

## License

MIT.

## AI disclosure

This program was written with Claude.
