# R AppMarket

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Italiano** · [Français](README.fr.md) · [Español](README.es.md)

R AppMarket ti permette di cercare e installare tutte le app di ogni repository configurato, da una finestra, invece di digitare comandi `pkgman` nel Terminal.

![R AppMarket](screenshot.png)

## Installazione

Disponibile per **arm64**, **x86_64** e **x86** a 32 bit (ibrido gcc2).

1. Apri **Terminal**: fai clic sul menu con la piuma in alto a destra, poi **Applications > Terminal**.
2. Aggiungi il repository della tua architettura, poi installa (non sai qual è? te lo dice `ls /system/packages/haiku-*.hpkg`):

   Su **arm64** o **x86_64**:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64    # oppure .../x86_64
   pkgman refresh
   pkgman install rappmarket
   ```

   Su **x86** a 32 bit (ibrido gcc2), il pacchetto si chiama `rappmarket_x86`:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2
   pkgman refresh
   pkgman install rappmarket_x86
   ```

3. Se il Terminal fa una domanda, digita **y** e premi **Invio**.
4. Al termine, sul Desktop comparirà l'icona di **R AppMarket**. Fai doppio clic per cercare e installare le app.

## Licenza

MIT.

## Nota sull'uso dell'IA

Questo programma è stato scritto con Claude.
