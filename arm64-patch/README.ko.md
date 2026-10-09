# arm64 Haiku 미디어 스택 패치

`@minimum-mmc` 프로파일로 빌드한 arm64 Haiku에 오디오 경로를 만들어 줍니다 -
`/dev/audio`, `media_server`, 그리고 녹음·재생 프로그램이 필요로 하는
애드온들입니다.

[English](README.md)

## 문제

`minimum` 이미지 정의는 미디어 프로그램만 빼는 것이 아니라 미디어 스택 자체를
제거합니다.

```
SYSTEM_ADD_ONS_DRIVERS_AUDIO = ;
SYSTEM_ADD_ONS_MEDIA = ;
```

`SYSTEM_SERVERS`에도 `media_server`가 없습니다. 이 정의로 만든 이미지는
`/dev/audio` 자체가 없는 상태로 부팅하고, 직렬 로그에 이렇게 남습니다.

```
Launching x-vnd.haiku-media_server failed: No such file or directory
```

미디어 킷을 여는 프로그램은 오디오 장치가 없다고 보고합니다. 드라이버 문제가
아니며 나중에 무엇을 설치해도 고쳐지지 않습니다. `media_server`는 `haiku`
패키지의 구성 요소라, 어느 프로파일로 이미지를 만들었느냐가 그 존재를
결정합니다.

`@nightly-mmc`에는 미디어 스택이 들어 있지만 arm64에서는 빌드되지 않습니다.

- `libmidi.so`는 `fluidlite` 빌드 기능이 켜져 있을 때만 만들어지는데 arm64에는
  그 패키지가 없습니다. 이미지 정의는 이 라이브러리를 무조건 요구하므로 jam이
  `don't know how to make libmidi.so`로 멈춥니다.
- WonderBrush 번역기의 Jamfile이 자기 `support/` 디렉터리를 헤더 검색 경로에
  넣지 않아 `bitmap_compression.h`와 `blending.h`를 찾지 못합니다.

## 패치가 바꾸는 것

`patches/0001-minimum-media-stack.patch`가 `minimum` 정의에 더하는 것들입니다.

| 추가 | 이유 |
| --- | --- |
| `media_server`, `media_addon_server` | 서버 자체가 없었습니다 |
| `libmedia.so` | 서버와 응용 프로그램이 링크합니다 |
| `libgame.so` | `media_addon_server`가 요구하며, 없으면 적재에 실패합니다 |
| `hda`, `usb_audio` | 오디오 드라이버 목록이 비어 있었습니다 |
| `hmulti_audio.media_addon`, `mixer.media_addon` | 미디어 애드온 목록이 비어 있었습니다 |

`patches/0002-wonderbrush-support-headers.patch`는 WonderBrush 번역기의 헤더
검색 경로를 고칩니다. `@minimum-mmc`에는 필요 없지만, `@nightly-mmc`를
빌드하려는 사람이 반드시 부딪히는 실제 버그이므로 함께 넣었습니다.

`patches/0003-efi-cd-boot.patch`는 EFI 대상에서 `@minimum-cd`가 빌드되게
합니다. 빌드 수정일 뿐입니다 - 그 ISO는 부트 로더를 띄우지만 데스크톱까지
부팅되지 않습니다. 이유는
[arm64에서 설치 가능한 매체](#arm64에서-설치-가능한-매체)에 있습니다.

`patches/0004-arm64-anyboot.patch`는 arm64에서 `@minimum-anyboot`이 빌드되게
합니다. 부팅도 되고 거기서 설치도 되는 매체를 내놓는 쪽은 이 패치입니다.


### 오디오 너머: 0005-0013

설치된 arm64 시스템에서 모든 앱을 시험하며 드러난 문제를, 이미지 안에서
우회하지 않고 이미지 자체에서 고친 것들입니다.

| 패치 | 무엇이 잘못돼 있었나 |
| --- | --- |
| `0005-arm64-rtc-and-psci` | 매 부팅마다 시계가 1970년에서 시작해(PL031을 읽지 않음) 어떤 TLS 인증서도 유효하지 않았고, 종료/재부팅이 끝나지 않았습니다(PSCI 호출 없음). haiku-rwebpositive-arm64에서 가져와 맞췄습니다. |
| `0006-arm64-rchromium-fixes` | R Chromium에 필요한 커널·runtime_loader 수정이 별도 시스템 패키지에만 있었고, 그 패키지에는 미디어 스택이 없었습니다. 이제 이 이미지의 `haiku` 패키지에 들어 있고 `haiku_rchromium_fixes`를 제공합니다. |
| `0007-arm64-pte-and-layer-text` | haiku-rwebpositive-arm64의 수정 두 가지(페이지 테이블 조회, app_server 레이어 안 텍스트 경계). |
| `0008-minimum-image-certs-and-repos` | 루트 인증서가 없었고, arm64에 존재하지 않는 HaikuPorts 저장소 설정이 있었으며, Haiku 자체 저장소의 더 새 `haiku` 패키지가 `pkgman update` 때 이 패치된 패키지를 덮어쓰게 돼 있었습니다. 이제 인증서가 들어가고 대신 RENKU Apps 저장소가 미리 설정됩니다. |
| `0009-arm64-zstd-feature` | packagefs가 zstd 패키지를 읽지 못했습니다. HaikuPorts 패키지 전부(인증서 포함)가 zstd입니다. `packages/`의 세 패키지가 필요합니다. |
| `0010-media-recorder-start-producer` | 녹음 앱이 사운드카드에 연결은 되지만 버퍼를 하나도 받지 못했습니다. BMediaRecorder가 생산자를 시작하지 않았습니다. |
| `0011-app-server-cjk-fallbacks` | "Noto Sans CJK JP"라는 글꼴이 없으면 한국어·일본어·중국어가 네모로 나왔는데, arm64에서는 그 글꼴을 설치할 수 없습니다. |
| `0012-minimum-cli-tools` | 새로 설치한 게스트는 pkgman 말고는 아무것도 받을 수 없었습니다. curl·wget·tar·gzip·grep·sed·python 이 없고 bash 에는 `/dev/tcp` 가 없습니다. grep·less·sed(부트스트랩 패키지)와 curl·wget·tar·gzip(pkgman-repo `scripts/cross-arm64-cli.sh` 로 크로스 빌드, hpkg 는 `packages/`)을 넣습니다. `HAIKU_NO_DOWNLOADS=1` 로 빌드합니다. |
| `0013-kernel-sock-nonblock` | `socket(SOCK_NONBLOCK)` 이 fd 에만 O_NONBLOCK 을 표시하고 소켓은 차단 모드로 남겼습니다. curl 은 매번 서버의 유휴 제한(30~400초)까지 `recv()` 에서 기다렸습니다. 이제 `fcntl(F_SETFL)` 처럼 스택에도 알립니다. |
| `0014-minimum-webpositive` | WebPositive가 이미지에 아예 없었습니다: `@minimum-mmc`는 절대 빌드하지 않고(브라우저 엔진 전체를 링크해야 하는 `haikuwebkit_devel`이 필요), `@minimum-anyboot`도 같은 정의를 물려받습니다. 0012가 curl/wget을 컴파일 대신 미리 빌드된 걸로 넣듯, 이미 빌드된 `webpositive` 패키지를 그대로 넣습니다. `packages/`의 `haikuwebkit`, `sqlite3`, `dav1d`, `libavif1.0`, `noto_sans_cjk_kr`도 함께 필요합니다. 또한 arm64 저장소 목록에 `openssl3`/`openssl3_devel`을 선언하는데, 이게 통째로 빠져 있었습니다: 0008이 이미 `AddHaikuImageSystemPackages`에 `openssl3`을 넣지만, 매칭되는 저장소 항목이 한 번도 선언된 적이 없어서 `HAIKU_NO_DOWNLOADS=1`에서 `AddRepositoryPackage`가 아무 에러 없이 조용히 빠뜨렸고 -- 로컬 인덱스에 그냥 없는 것뿐 -- 의존성 solver가 `haikuwebkit`의 `lib:libcrypto` 요구를 채울 게 없었습니다. 지금까지 됐던 건 어느 예전 빌드의 `download/`에 누군가 손으로 hpkg를 넣어뒀던 게 패치로 한 번도 캡처되지 않은 채 남아있었기 때문입니다. |
| `0015-arm64-webpositive-icu-data` | WebPositive가 실제 텍스트 레이아웃이 필요한 페이지(구글 홈페이지 등)를 열면 바로 죽었습니다: `WTF::TextBreakIteratorICU` 생성자가 ICU의 `ubrk_open()`이 실패하면 `RELEASE_ASSERT`를 겁니다. 빈/루트 로케일로도 실패했습니다. 원인: arm64의 부트스트랩 프로파일 `icu74` 패키지가 담고 있는 `libicudata.so.74`가 약 130KB짜리 스텁이라서, WebKit의 텍스트 분리에 필요한 약 30MB짜리 데이터셋이 빠져 있습니다 -- 그 전체 데이터셋은 `data/icu/74.1/icudt74l.dat`로 패키지 안에 같이 들어있지만 아무도 안 씁니다. 수정: `data/system/boot/SetupEnvironment`가 arm64에서 `ICU_DATA`를 그 경로로 export합니다 -- ICU 자체가 공식 지원하는 대체 경로 지정 방법입니다. |
| `0016-arm64-openssh` | arm64에는 `ssh`/`sshd`가 아예 없었습니다 -- HaikuPorts의 진짜(부트스트랩 아닌) arm64 저장소 자체가 비어있습니다. `packages/openssh-10.4p1-1-arm64.hpkg`는 0012의 curl/wget과 같은 방식으로, HaikuPorts 자체의 net-misc/openssh 패치셋을 써서 크로스 빌드했습니다. libedit은 뺐습니다(arm64 패키지 목록에 없고, sftp 줄 편집 기능에만 쓰임). `data/launch/sshd`(job/service 쌍, `data/system/boot/SshdKeygen`이 먼저 호스트 키를 만든 뒤)로 부팅 시 시작하며, 둘 다 `build/jam/packages/Haiku`에 새로 등록했습니다 -- `SetupEnvironment`/`data/launch/system`/`user`와 마찬가지로, `data/` 밑의 파일이 실제로 이미지에 들어가는 유일한 방법입니다. 서비스는 `launch`로 직접이 아니라 `/bin/sh -c "sshd -D"`로 감싸서 실행합니다 -- 취향이 아니라 실패를 겪어서 정해졌습니다: 직접 launch하면 sshd 자신의 디버그 로그엔 listening 중이라고 나오는데도 모든 연결을 거부하고, `sh -c` 한 겹만 거치면 매번 제대로 됩니다. launch_daemon 내부까지 원인을 찾지는 못했습니다. |

## 사용법

```sh
../build-renku-arm64-iso.sh
```

`patches/`의 모든 패치를 적용하고, `packages/*.hpkg`를 생성 디렉터리의
`download/`(0009의 zstd 패키지와 0014의 WebPositive 세트가 필요로 하는
바로 그 자리)에 넣은 뒤 `@minimum-anyboot`를 빌드합니다. 중요한 의미에서
멱등합니다: 항상 고정된 커밋을 새로 체크아웃하는 데서 시작하므로 절반만
패치된 트리가 생길 여지가 없습니다.

다른 RenkuOS/Source 체크아웃에(예: `@minimum-mmc`용으로) 손으로 패치를
적용하고 싶다면 `git apply`를 평소처럼 쓰면 됩니다:

```sh
for p in patches/*.patch; do git -C /path/to/haiku apply "$p"; done
cp packages/*.hpkg /path/to/generated.arm64/download/
```

## 확인

RenkuOS/Source의 커밋
[`47367b99ab8af5934adb4e709e9c0eaeb6485255`](https://github.com/RenkuOS/Source/commit/47367b99ab8af5934adb4e709e9c0eaeb6485255)
(2026-09-03)에서 뽑아내고 같은 커밋으로 검증했습니다. 2026-09-29 기준으로
`nightly` 태그가 가리키던 커밋입니다. 이 태그는 움직이므로, 충분히 옮겨가면
패치가 더 이상 적용되지 않습니다. 그때 `build-renku-arm64-iso.sh`는 반쯤
적용된 트리를 남기지 않고 그 사실을 알립니다.

그 커밋으로 빌드해 Apple Silicon의 QEMU에서 부팅했습니다
(`qemu-system-aarch64`, `hvf`, `-device intel-hda -device hda-duplex`).

```
HDA: Detected controller @ PCI:0:3:0, IRQ:38, type 8086/2668
hda: HDA v1.0, O:4/I:4/B:0, #SDO:1, 64bit:yes
loaded driver /boot/system/add-ons/kernel/drivers/dev/audio/hmulti/hda
```

`ls /dev/audio`에 `hmulti`가 나오고, 시스템 오디오 입력을 받는 스펙트럼
분석기 R Spectrum이 `No audio input device` 대신
`96000 Hz  48 bands  peak -78.0 dBFS`를 표시합니다.


## arm64에서 설치 가능한 매체

`patches/0004-arm64-anyboot.patch`로 arm64에서 `@minimum-anyboot`이 빌드됩니다.
x86에서 그 프로파일이 주는 것과 같은 것을 줍니다 - 부팅되는 파일 하나, 그리고
그것으로부터 별도 디스크에 설치.

```sh
../build-renku-arm64-iso.sh       # 패치 적용과 아래 jam 두 단계를 알아서
                                   # 다 함; 주의사항 이름 붙이려고 따로 적음
jam -q -j8 haiku-boot-cd          # 아래 주의사항 참고
jam -q -j8 '@minimum-anyboot'     # -> haiku-minimum-anyboot.iso
```

결과물은 하이브리드입니다. UEFI El Torito 항목을 가진 ISO9660 이미지이면서,
동시에 실제 BFS 파티션과 ESP를 담은 MBR 파티션 디스크입니다.

```
MBR signature 0xaa55
  part0  type 0xeb  bootable  offset 4194304   300.0 MiB  (BFS, "Haiku")
  part1  type 0xef            offset 318767104   2.8 MiB  (FAT32, ESP)
El Torito boot img : 1 UEFI -> /esp.image
```

핵심은 그 BFS 파티션입니다. `haiku_loader`에는 iso9660 드라이버가 없어 순수
ISO9660 매체에서는 시스템을 결코 읽지 못합니다 - 평범한 `@minimum-cd` ISO는
로더를 띄운 뒤 커널이 올라가기도 전에
*"Cannot continue booting (Boot volume is not valid)"*에서 멈춥니다. 여기서는
로더가 같은 매체 위에서 자기가 읽을 수 있는 파일시스템을 찾아 정상 부팅합니다.
부팅된 시스템의 DriveSetup에서 확인됩니다 - 그 ISO가 디스크로 보이고,
파티션 0이 Be File System이며 `/boot`에 마운트되어 있습니다.

### 거기서 설치하기

Apple Silicon의 QEMU에서 anyboot ISO를 USB 저장장치로, 빈 8 GB 디스크를
대상으로 붙여 끝까지 확인했습니다.

1. DriveSetup에서 대상 디스크를 **GUID Partition Map**으로 초기화하고,
   *EFI system data* 형식의 64 MiB 파티션을 만들어 FAT32로 포맷한 뒤,
   나머지 전부를 Be File System 파티션으로 만듭니다.
2. Installer로 BFS 파티션에 설치합니다.
   *"Installation completed. Boot sector has been written to ..."*가 나옵니다.
3. 새 ESP에 부트 로더를 직접 복사합니다. EFI에서는 Installer가 이 일을
   하지 않습니다.

   ```sh
   mountvolume -all
   cp -r "/haiku esp/EFI" "/new fat vol/"
   ```

4. 매체를 떼고 디스크로 부팅합니다. `/boot`가 설치된 BFS 볼륨이 되고,
   `ps`에 `media_server`와 `media_addon_server`가 보입니다.

`@minimum-mmc`가 내놓는 raw `.image` / `.mmc` 매체도 같은 방식으로 동작하며,
SD 카드에 쓸 때는 여전히 그쪽이 낫습니다.

### 두 가지 주의사항

- **디스크는 AHCI가 아니라 USB로 붙이십시오.** QEMU의 `edk2-aarch64`
  펌웨어에는 SATA 드라이버가 없어서 `-device ahci`로는 부팅 장치를 아예 찾지
  못하고 `map: No mapping found`와 함께 UEFI 셸로 떨어집니다. Haiku 자체는
  일단 부팅하면 AHCI 디스크를 잘 봅니다. 그것으로 부팅하지 못하는 쪽은
  펌웨어입니다.
- **`@minimum-anyboot` 전에 `jam haiku-boot-cd`를 먼저 돌리십시오.**
  `haiku-boot-cd.iso`가 `RmTemps` 대상이라 직전 실행이 그것을 지우고, 다음
  실행이 `failed to open ISO file`로 실패합니다. 이 패치들이 만든 문제가
  아니라 업스트림의 기존 문제입니다 - `AnybootImage`의 TODO 주석이 이미 이것을
  가리키고 있습니다.

## 알려진 한계: WebPositive에서 오디오/비디오 재생 안 됨

`haikuwebkit`의 `buildMediaEnginesVector()`(`Source/WebCore/platform/graphics/MediaPlayer.cpp`)가
Cocoa, GStreamer, Media Foundation, HolePunch 미디어 엔진은 등록하면서 Haiku용은
하나도 등록하지 않습니다 -- `PlatformMediaEngineClassName`이 `PLATFORM(HAIKU)`에
정의는 돼 있는데 등록에 쓰이질 않습니다. `installedMediaEngines()`가 비어서
`canPlayType()`이 항상 빈 문자열을 돌려주고, WebPositive는 HTML5 오디오·비디오를
전혀 재생하지 못합니다(유튜브: "this browser can't play this video"). x86의
HaikuWebKit 1.9.19, 1.9.26에서 확인됐고, 여기 arm64 이미지는 같은 코드베이스의
1.10.0을 쓰므로 거의 확실히 같은 문제가 있을 텐데 직접 확인하지는 않았습니다.

이건 HaikuWebKit 소스 자체의 결함이라 `0014`나 `packages/`의 어떤 패키지로도
고칠 수 없습니다 -- 여기서 `haikuwebkit`은 미리 빌드된 걸 그대로 쓸 뿐, 이 트리가
소스에서 컴파일하지 않습니다. 고치려면 HaikuWebKit 자체를 패치하고
`haikuwebkit`/`haikuwebkit_devel` 패키지를 다시 빌드해야 하는, 이 이미지와는
별개의 작업입니다. (병렬로 진행 중인 다른 세션이 x86용으로 고치고 있으니, 여기서
다시 조사하기 전에 그 수정이 1.10.0까지 반영됐는지 먼저 확인해볼 가치가 있습니다.)

## 알려진 한계: SSH 비밀번호 인증

`ssh`/`sshd`는 작동합니다 -- 키 기반 로그인은 호스트에서 게스트까지
(`run-qemu-renku-arm64.sh`가 이미 설정해둔 `hostfwd=tcp::2222-:22`로) 끝까지
확인했습니다. 비밀번호 인증은 안 됩니다: 어떤 비밀번호를 넣어도 거부되는데,
`sshd -d -d -d`로 보면 방금 `passwd`로 설정한 비밀번호에도
`mm_answer_authpassword: sending result 0`가 찍힙니다.

원인: Haiku 자체 `crypt()`(`src/system/libroot/posix/crypt/crypt.cpp`)는
Haiku 전용 scrypt 기반 해시(`$s$<n>$<salt>$<hash>`)를 쓰고, 실제 비밀번호
저장소는 POSIX shadow 파일이 아니라 BMessage로 접근하는 레지스트라
서비스입니다 -- `getspnam()`은 그 위에 얹은 호환 레이어일 뿐, 실제 파일을
읽는 게 아닙니다. OpenSSH의 비밀번호 인증 경로(`auth-passwd.c` /
`openbsd-compat/xcrypt.c`)는 POSIX shadow 파일 모델을 전제로 짜여 있어서,
이 간극은 한 줄로 고칠 수준이 아니라 여기선 시도하지 않았습니다. 키 기반
로그인은 영향받지 않고, 이 이미지에서 지원하는 방법입니다.

## 라이선스

MIT

## AI 사용 고지

이 패치들은 Claude와 함께 준비했습니다.
