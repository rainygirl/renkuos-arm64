# R AppMarket

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · [Italiano](README.it.md) · [Français](README.fr.md) · **Español**

R AppMarket te permite explorar e instalar todas las apps de cada repositorio configurado, desde una ventana, sin tener que escribir comandos `pkgman` en Terminal.

![R AppMarket](screenshot.png)

## Instalación

Disponible para **arm64**, **x86_64** y **x86** de 32 bits (híbrido gcc2).

1. Abre **Terminal**: haz clic en el menú de la pluma arriba a la derecha de la pantalla y luego en **Applications > Terminal**.
2. Añade el repositorio de tu arquitectura y luego instala (¿no sabes cuál es? `ls /system/packages/haiku-*.hpkg` te lo dice):

   En **arm64** o **x86_64**:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/arm64    # o .../x86_64
   pkgman refresh
   pkgman install rappmarket
   ```

   En **x86** de 32 bits (híbrido gcc2), el paquete se llama `rappmarket_x86`:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2
   pkgman refresh
   pkgman install rappmarket_x86
   ```

3. Si Terminal te hace una pregunta, escribe **y** y pulsa **Intro**.
4. Cuando termine, aparecerá un icono de **R AppMarket** en el Escritorio. Haz doble clic para explorar e instalar apps.

## Licencia

MIT.

## Aviso sobre el uso de IA

Este programa fue escrito con Claude.
