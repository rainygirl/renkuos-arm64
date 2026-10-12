# R AppMarket

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Italiano** · [Français](README.fr.md) · [Español](README.es.md)

R AppMarket ti permette di cercare e installare le app R* di RENKU da una finestra, invece di digitare comandi `pkgman` nel Terminal.

![R AppMarket](screenshot.png)

## Installazione

Disponibile per **arm64**, **x86_64** e **x86** a 32 bit (ibrido gcc2).

1. Apri **Terminal**: fai clic sul menu con la piuma in alto a destra, poi **Applications > Terminal**.
2. Aggiungi il repository della tua architettura (non sai qual è? te lo dice `ls /system/packages/haiku-*.hpkg`), poi installa:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64      # arm64
   pkgman add-repo https://pkgman.rainygirl.com/x86_64     # x86 a 64 bit
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2   # x86 a 32 bit (ibrido gcc2)

   pkgman refresh
   pkgman install rappmarket
   ```

3. Se il Terminal fa una domanda, digita **y** e premi **Invio**.
4. Al termine, sul Desktop comparirà l'icona di **R AppMarket**. Fai doppio clic per cercare e installare le app.

## Licenza

MIT.

## Nota sull'uso dell'IA

Questo programma è stato scritto con Claude.
