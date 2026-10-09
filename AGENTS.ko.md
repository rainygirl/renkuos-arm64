# RENKU arm64 -- 기술 노트

[English](AGENTS.md)

`README.md`에는 없는 내용을 담았습니다: 빌드가 어떻게 돌아가는지, 패치가
뭘 하는지, 손대기 전에 알아둬야 할 함정들. AI 에이전트가 이 디렉터리에서
작업한다면 뭘 하기 전에 이 파일부터 읽으세요.

## 명령어

이 안에 스크립트는 정확히 두 개뿐입니다. 세 번째 스크립트가 보인다면
그건 누군가 작업하다 남긴 것이니 지워도 됩니다.

```sh
./build-renku-arm64-iso.sh              # -> ./renku-arm64.iso
SKIP_CROSS_TOOLS=1 ./build-renku-arm64-iso.sh   # 캐시된 툴체인 재사용
```

"부팅해줘", "실행해줘", "QEMU 켜줘" 같은 요청을 받으면 이걸 쓰세요:

```sh
./run-qemu-renku-arm64.sh                # 설치된 디스크를 창으로 부팅
./run-qemu-renku-arm64.sh --install      # ISO를 디스크와 함께 부팅(설치용)
./run-qemu-renku-arm64.sh --headless     # 창 없이 부팅
```

`qemu-system-aarch64` 명령줄을 직접 조립하지 마세요.
`run-qemu-renku-arm64.sh`가 이 포팅에 꼭 필요한 설정을 이미 다 해뒀습니다:

- 디스크는 USB로 붙입니다. QEMU의 edk2-aarch64 펌웨어는 SATA 드라이버가
  없어서, AHCI로 붙이면 펌웨어가 디스크를 아예 못 보고 UEFI Shell로
  떨어집니다.
- 설치 매체와 타겟 디스크는 서로 다른 xhci 컨트롤러에 연결합니다. 한
  컨트롤러에 장치 네 개를 몰아 붙이면 게스트의 USB 키보드/태블릿이
  망가집니다.
- hvf 위에서는 `-cpu host`를 씁니다.

이 중 하나라도 틀리면 에러 메시지 없이 그냥 멈추거나, 입력이 아예 안
먹히는 상태가 됩니다. 디스크 파일(`renku-arm64-vm.img`)은 실행할 때마다
그대로 남아있습니다.

## 패치

`arm64-patch/patches/`에는 RenkuOS/Source에 적용하는 패치 16개가 있고,
`build-renku-arm64-iso.sh`가 빌드할 때마다 자동으로 적용합니다. 각 패치가
정확히 뭘 바꾸고 왜 그런지는
[`arm64-patch/README.ko.md`](arm64-patch/README.ko.md)에 파일 단위로 자세히
적어뒀습니다. 여기서는 전체적인 모양만 간단히 설명합니다.

RenkuOS/Source의 `@minimum-mmc`/`@minimum-anyboot` 빌드 프로파일은
아키텍처와 상관없이 일부러 최소한의 기능만 담습니다. 미디어 스택도
없고, 브라우저도 없고, CLI 도구도 거의 없습니다. 패치는 크게 세 가지
일을 합니다:

1. 빠진 걸 다시 채웁니다 (0001, 0012, 0014).
2. arm64에서만 터지는 버그 세 개를 고칩니다: PL031 시계와 PSCI 전원 끄기
   (0005), 커널/runtime_loader/app_server 수정 (0006, 0007), packagefs가
   HaikuPorts 패키지를 읽을 수 있도록 zstd 지원 추가 (0009).
3. `@minimum-anyboot`가 arm64에서도 빌드되게 만듭니다 (0003, 0004). 업스트림
   anyboot 이미지는 BIOS와 EFI를 겸하는데, 그중 세 부분이 x86 전용이라
   arm64에서는 원래 빌드가 안 됩니다.

업스트림 트리가 옮겨가서 패치가 더 이상 안 먹을 때는, RenkuOS/Source를
체크아웃해서 같은 수정을 직접 적용한 다음 그 트리와 패치가 검증됐던
기준 커밋 사이의 diff를 뜨면 됩니다. 각 패치가 어느 커밋에서 검증됐는지는
패치 파일 안에 적혀 있습니다.

## 빌드 파이프라인

`build-renku-arm64-iso.sh`는 **호스트와 같은 CPU 아키텍처**의 Docker
컨테이너 안에서 전부 돌아갑니다. Apple Silicon이면 arm64 컨테이너, Intel
이면 amd64 컨테이너를 씁니다.

원래부터 이렇게 설계한 건 아니었습니다. 처음엔 Apple Silicon에서도
`linux/amd64` 컨테이너를 강제로 썼는데, 그러니까 빌드 전체가 불안정해졌습니다.
Rosetta로 돌리든 QEMU 자체 번역으로 돌리든, gcc가 가끔 멀쩡한 번역
단위를 에뮬레이션 중에 잘못 컴파일했고, 그것도 실행할 때마다 다른
파일에서 터졌습니다(`libiberty`의 `vprintf-support.c`, 그다음엔
`pexecute.o`, 그다음엔 zlib의 `gzlib.o`). 같은 소스가 네이티브에서는
깨끗하게 빌드된다는 걸 확인하기 전까지는 영락없이 컴파일러 버그처럼
보이는 증상이었습니다. 컨테이너를 호스트 CPU에 맞추고 나니 안정성과
속도 문제가 둘 다 해결됐습니다.

빌드에는 `HAIKU_NO_DOWNLOADS=1`이 꼭 필요합니다. 패치가 arm64 저장소의
패키지 목록을 바꾸면 그 파일의 체크섬도 바뀌는데, 이 변수가 없으면 jam이
그 새 체크섬으로 된 인덱스를 Haiku CDN에 요청하다가 404를 받습니다. 이
변수를 켜면 jam이 `download/` 디렉터리 안의 파일만으로 저장소 인덱스를
직접 만들기 때문에, 빌드를 시작하기 전에 `arm64-patch/packages/*.hpkg`를
그 디렉터리에 미리 넣어둬야 합니다. 이건 스크립트가 자동으로 해줍니다.

이 과정에서 실제로 겪었던 문제 하나를 적어둡니다. `openssl3`과
`openssl3_devel`은 루트 인증서 수정을 위해 `AddHaikuImageSystemPackages
openssl3`로 이미 image에 추가돼 있었는데, 정작 arm64 저장소 목록에는 한
번도 선언된 적이 없었습니다. 평소처럼 네트워크에서 패키지를 받아오는
빌드에서는 이 문제가 전혀 드러나지 않았습니다. 하지만
`HAIKU_NO_DOWNLOADS=1`을 켠 채로 깨끗한 `download/`에서 빌드하면,
`AddRepositoryPackage`는 매칭되는 파일이 없는 패키지를 에러도 없이 조용히
건너뜁니다. 그냥 로컬 인덱스에 그 패키지가 없는 것뿐이라서, 겉으로는
아무 문제가 없어 보입니다. 이 문제는 `haikuwebkit`이 `lib:libcrypto`를
요구하는 시점이 돼서야, 의존성 solver가 줄 게 없다는 형태로 드러났습니다.
`0014-minimum-webpositive.patch`에서 이 두 패키지를 제대로 선언해서
고쳤고, 같은 패치에서 WebPositive 자체와 HaikuWebKit, 그 외 런타임
의존성(sqlite3, dav1d, libavif1.0, noto_sans_cjk_kr)도 같은 방식으로
선언합니다.

같은 "조용히 건너뛰기" 동작 때문에, `download/`에는 저장소 파일이 선언한
패키지가 **전부** 있어야 합니다. `arm64-patch/packages/`에 있는, 이
프로젝트 고유의 패키지(curl, webpositive, openssh 등)만으로는 부족합니다.
freetype, ncurses6, gcc, icu74처럼 이 포팅과는 무관한 일반 업스트림
부트스트랩 패키지도 수십 개 더 필요한데, 이런 건 이 프로젝트 고유의
것이 아니라서 저장소에 커밋해두지 않았습니다.

이전에 빌드해본 적이 있는 `haiku-builder` 컨테이너는
`generated.arm64/download/`에 이 패키지들이 이미 남아있어서 아무 문제가
없습니다. 하지만 컨테이너를 새로 만들거나 다른 머신에서 빌드하면, 가장
오래 걸리는 컴파일 단계에 들어간 지 몇 분 만에 `fatal error: float.h: No
such file or directory` 같은, 진짜 원인과는 전혀 상관없어 보이는 에러로
멈춥니다.

`build-renku-arm64-iso.sh`는 이제 이 문제를 스스로 해결합니다. 트리를
체크아웃한 직후, 그러니까 패치를 적용해서 체크섬이 바뀌기 전에
`build/jam/repositories/HaikuPorts/arm64` 파일의 해시를 미리 구해둡니다.
그 다음 `arm64-patch/packages/`를 `download/`에 스테이징한 뒤, (이제
패치가 적용된) 그 저장소 파일을 파싱해서 선언된 패키지를 전부 찾아내고,
`download/`에 없는 패키지는
`https://eu.hpkg.haiku-os.org/haikuports/master/build-packages/<그 해시>/packages/<name>-<version>-<arch>.hpkg`
에서 직접 받아옵니다. 이 URL 형식은 `build/jam/RepositoryRules`의
`RemoteRepositoryFetchPackage`가 실제로 쓰는 것과 똑같은데, 다만 이
포팅의 패치가 적용되기 전, 즉 그 패키지들이 실제로 공개됐던 시점의
체크섬을 씁니다.

`arm64-patch/patches/`가 직접 추가한 패키지(curl, webpositive, openssh
등)는 이 체크섬으로는 어디에도 공개된 적이 없어서, 혹시 요청하면 404가
납니다. 하지만 이 fetch 단계가 돌 때는 이미 스테이징이 끝난 뒤라서, 그런
패키지는 애초에 요청 대상에 들어가지 않습니다. 정말로 더 이상 구할 수
없는 패키지가 있으면(예: `curl_source` -- 크로스 빌드한 curl이 HaikuPorts
것을 대체하기 때문에 소스 패키지로 공개된 적이 없습니다) 경고만 찍고
넘어가며, 빌드 전체를 실패시키지는 않습니다. 어차피 `AddRepositoryPackage`
자체가 없는 패키지를 조용히 건너뛰므로, 정말 필요 없는 패키지라면 나중
단계에서도 문제가 되지 않습니다.

## QEMU에서 겪은 함정들

아래는 전부 취향이 아니라 실제로 겪은 실패를 보고 정한 것들입니다.

- **부팅/설치 매체는 반드시 USB로 붙이세요.** EDK2의 edk2-aarch64
  펌웨어는 SATA 드라이버가 없습니다. 그래서 AHCI로 디스크를 붙이면
  펌웨어가 그 디스크를 아예 못 보고, `map: No mapping found`를 띄우며
  UEFI Shell로 떨어집니다. Haiku 자체 드라이버는 커널이 일단 돌기
  시작하면 AHCI 디스크를 문제없이 봅니다. 시작을 못 하는 건 펌웨어뿐입니다.

- **USB 저장장치를 두 개 붙이면 부팅 스플래시에서 멈출 수 있습니다.**
  단, 두 번째 장치에 실제로 읽을 수 있는 파일시스템이 있을 때만
  그렇습니다. 비어 있거나 파티션이 안 된 두 번째 USB 디스크는 문제없이
  돌아갑니다. `--install` 옵션이 평소에 붙이는 디스크가 바로 이런
  상태인데, 원래 목적 자체가 그 디스크를 파티션하는 거라서 그렇습니다.
  문제는 이미 설치가 끝난 디스크를 ISO와 함께 다시 붙였을 때
  재현됩니다. 설치된 디스크를 라이브 미디엄과 비교하거나 점검해야
  한다면, 그 디스크는 `-device ahci`(그 아래 `ide-hd`)로 붙이세요. 일단
  부팅만 되면 Haiku가 그 디스크도 잘 봅니다.

- **QEMU를 끄기 전에는 반드시 게스트를 정상적으로 종료하세요.** 설치
  절차에서 진짜로 지켜야 할 건 사실상 이것 하나뿐입니다. 두 번 재현해서
  확인한 내용입니다: `Installer`가 끝나자마자, 또는 EFI 로더를 복사하고
  나서 곧바로 QEMU를 강제로 닫으면(QMP로 `quit`을 보내든, 창을 그냥
  닫든) Haiku의 쓰기 캐시가 디스크에 아직 반영되지 않은 상태로 남습니다.
  그러면 다음 부팅 때 이런 일이 생깁니다.

  - 복사해둔 `BOOTAA64.EFI`가 깨진 채로 저장될 수 있습니다. 파일
    크기는 바이트 단위까지 똑같은데 MD5는 다릅니다. EFI Shell에서 이
    파일을 실행하면 `Command Error Status: Unsupported`가 뜹니다.
  - 시스템 패키지도 같은 식으로 깨질 수 있습니다(실제로 본 사례는
    `zstd-1.5.6-2-arm64.hpkg`). `libbe.so`가 그 패키지 안의
    `libzstd.so.1`을 링크하기 때문에, 패키지가 깨지면 `launch_daemon`이
    시작을 못 하고 부팅이 스플래시 화면에서 멈춥니다. 시리얼 로그에는
    `runtime_loader: Cannot open file libzstd.so.1 ... error starting
    "/boot/system/servers/launch_daemon"`이 찍힙니다.

  둘 다 **파일 복사가 깨진 증상이지, 패키징이나 packagefs 자체의
  버그가 아닙니다.** 같은 설치를 다시 하고, 이번엔 QEMU를 끄기 전에
  Deskbar > Shutdown > Power off로 정상 종료했더니, 바이트 단위로
  동일한 파일(MD5로 확인)과 WebPositive까지 포함한 완전한 데스크탑으로
  첫 시도에 정상 부팅됐습니다. 게스트를 정상 종료하면 QEMU도 알아서
  꺼지므로(`arm64-patch`의 0005 패치가 넣은 PSCI 지원 덕분입니다),
  애초에 강제로 닫을 이유가 없습니다.

## 알려진 한계

**WebPositive에서는 HTML5 오디오와 비디오가 전혀 재생되지 않습니다.**
HaikuWebKit의 `buildMediaEnginesVector()`
(`Source/WebCore/platform/graphics/MediaPlayer.cpp`)는 Cocoa, GStreamer,
Media Foundation, HolePunch용 미디어 엔진은 등록하지만 Haiku용은 하나도
등록하지 않습니다. `PlatformMediaEngineClassName`이 `PLATFORM(HAIKU)`
아래 정의는 돼 있는데 실제 등록에는 쓰이지 않아서, 결국
`installedMediaEngines()`가 빈 목록을 돌려주고 `canPlayType()`도 항상
빈 값을 돌려줍니다. 이 문제는 HaikuWebKit 1.9.19와 1.9.26(x86)에서 확인된
것이고, 이 이미지는 같은 코드베이스의 1.10.0을 쓰기 때문에 거의 확실히
같은 문제가 있을 것으로 보이지만 여기서 직접 확인하지는 않았습니다.

이건 HaikuWebKit 소스 자체의 결함이라서, `0014`나 `packages/`에 있는
어떤 패키지로도 고칠 수 없습니다. 이 이미지는 `haikuwebkit`을 미리 빌드된
형태로 그대로 가져다 쓸 뿐, 소스에서 직접 컴파일하지 않습니다. 고치려면
HaikuWebKit 자체를 패치하고 `haikuwebkit`/`haikuwebkit_devel` 패키지를
다시 빌드해야 하는데, 이건 이 이미지와는 완전히 별개의 작업입니다.
(병렬로 진행 중인 다른 세션에서 x86용으로 이 문제를 고치고 있다고 하니,
여기서 다시 조사하기 전에 그 수정이 1.10.0까지 반영됐는지 먼저 확인해볼
가치가 있습니다.)

## 확인된 것

- **라이브 부팅** (ISO가 유일한 USB 디스크로 붙은 상태): 데스크탑,
  WebPositive, `curl`/`wget`/`grep`/`tar`/`gzip`, 그리고 실제 HTTPS
  요청(`curl -sI https://example.com` -> `200 OK`)까지 전부 정상 동작을
  확인했습니다.
- **설치** (DriveSetup -> Installer -> EFI 로더 복사 -> 정상 종료 ->
  설치된 디스크만 붙여서 재부팅): WebPositive까지 포함한 완전한
  데스크탑으로 부팅되는 것을 처음부터 끝까지 확인했습니다.
- **SSH**: `sshd`가 부팅 시 자동으로 시작합니다. 호스트에서 게스트로
  키 기반 로그인이 되는 것을 확인했습니다. 비밀번호 로그인은 Haiku의
  자체 인증 방식 때문에 안 됩니다(`arm64-patch/README.ko.md` 참고).
- **R\* 앱**: 부팅 후 호스팅된 저장소(https://pkgman.rainygirl.com/arm64)에서
  `pkgman`으로 설치합니다. ISO에 미리 들어있지는 않습니다.
