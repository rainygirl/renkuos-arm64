# RENKU arm64 -- 기술 노트

[English](AGENTS.md)

`README.md`에 없는 것들: 빌드가 어떻게 돌아가는지, 패치가 뭘 하는지, 손대기
전에 알아둘 함정들. AI 에이전트로서 이 디렉터리에서 작업 중이라면, 이
파일이 곧 당신의 CLAUDE.md입니다 -- CLAUDE.md는 이제 여기를 가리키는
포인터일 뿐입니다.

## 명령어

여기 스크립트는 정확히 두 개뿐입니다. 그 외엔 없음 -- 세 번째가 보이면
그건 스크래치 작업물이고 남아있으면 안 됩니다.

```sh
./build-renku-arm64-iso.sh              # -> ./renku-arm64.iso
SKIP_CROSS_TOOLS=1 ./build-renku-arm64-iso.sh   # 캐시된 툴체인 재사용
```

"부팅해줘", "실행해줘", "QEMU 켜줘" 같은 요청을 받으면:

```sh
./run-qemu-renku-arm64.sh                # 설치된 디스크를 창으로 부팅
./run-qemu-renku-arm64.sh --install      # 디스크를 붙인 채로 ISO 부팅
./run-qemu-renku-arm64.sh --headless     # 창 없이
```

`qemu-system-aarch64` 명령줄을 직접 조립하지 마세요. `run-qemu-renku-arm64.sh`가
이 포팅에 필요한 것들을 이미 고정해뒀습니다 -- USB로 붙인 디스크(QEMU의
edk2-aarch64 펌웨어는 SATA 드라이버가 없어서 AHCI는 안 보이고 UEFI
Shell로 떨어짐), 설치 매체와 타겟 디스크를 분리된 xhci 컨트롤러에
연결(한 컨트롤러에 네 개 달면 게스트 USB 키보드/태블릿이 깨짐), hvf에서
`-cpu host` -- 이 중 하나라도 틀리면 명확한 에러가 아니라 행이나 입력
안 되는 컨트롤러가 됩니다. 디스크(`renku-arm64-vm.img`)는 실행마다
그대로 남습니다.

## 패치

`arm64-patch/patches/`는 RenkuOS/Source에 대한 14개 패치이고,
`build-renku-arm64-iso.sh`가 적용합니다. 각각이 정확히 뭘 바꾸고 왜 그런지는
[`arm64-patch/README.ko.md`](arm64-patch/README.ko.md)에 파일 단위로
문서화돼 있습니다 -- 이 절은 전체 모양만 요약합니다.

RenkuOS/Source의 `@minimum-mmc`/`@minimum-anyboot` 프로파일은 어느
아키텍처에서든 일부러 최소한만 담습니다: 미디어 스택 없음, 브라우저 없음,
CLI 도구도 거의 없음. 패치는 그것들을 다시 채우고(0001, 0012, 0014),
arm64에서만 깨지는 세 가지를 고치고(0005 PL031 시계와 PSCI 전원 끄기, 0006과
0007 커널/runtime_loader/app_server 수정, 0009 zstd 패키지 지원으로
packagefs가 HaikuPorts가 주는 걸 읽을 수 있게 함), `@minimum-anyboot`가
arm64에서 아예 빌드되게 만듭니다(0003, 0004 -- 업스트림 anyboot 이미지는
BIOS+EFI 겸용이고 그중 세 조각이 x86 전용입니다).

패치를 다시 만들어야 할 때(업스트림 트리가 옮겨가서 더 이상 적용 안 될 때)는
RenkuOS/Source 체크아웃에 수정을 직접 적용한 뒤 고정된 기준 커밋과 diff를
뜨면 됩니다 -- 각 패치가 검증된 정확한 커밋은 개별 패치 파일에 있습니다.

## 빌드 파이프라인

`build-renku-arm64-iso.sh`는 **호스트 자신의 CPU 아키텍처**와 일치하는
Docker 컨테이너 안에서 전부 돌아갑니다(Apple Silicon이면 arm64, Intel이면
amd64). 원래 이렇게 설계된 건 아니었습니다 -- Apple Silicon에서
`linux/amd64`를 강제하는 걸 먼저 시도했는데, 빌드 전체가 불안정해졌습니다:
Rosetta와 QEMU 자체 번역 둘 다에서 gcc가 가끔 평범한 번역 단위 컴파일을
에뮬레이션 중에 깨뜨렸는데, 실행마다 다른 파일이었습니다(`libiberty`의
`vprintf-support.c`, 그다음 `pexecute.o`, 그다음 zlib의 `gzlib.o`). 같은
소스가 네이티브에서는 깨끗이 빌드된다는 걸 알기 전까진 영락없는 컴파일러
버그처럼 보입니다. 컨테이너를 호스트 CPU에 맞추니 안정성과 속도 둘 다
해결됐습니다.

`HAIKU_NO_DOWNLOADS=1`이 필요합니다: 패치가 arm64 저장소 패키지 목록을
바꾸면 체크섬이 바뀌는데, 이게 없으면 jam이 그 새 체크섬으로 된 인덱스를
Haiku CDN에 요청해서 404가 납니다. 이 변수를 켜면 jam이 `download/`에서
저장소 인덱스를 직접 만드는데, 그래서 빌드 전에
`arm64-patch/packages/*.hpkg`를 거기 미리 넣어둬야 합니다(스크립트가
자동으로 해줍니다).

이 과정에서 드러난 빈틈 하나: `openssl3`/`openssl3_devel`이 루트 인증서
수정을 위한 `AddHaikuImageSystemPackages openssl3`는 이미 있었는데도 arm64
저장소 목록에 한 번도 선언된 적이 없었습니다. 평소(다운로드하는) 빌드에서는
패키지가 네트워크에서 그냥 왔기 때문에 아무도 몰랐는데,
`HAIKU_NO_DOWNLOADS=1`에 깨끗한 `download/`로 빌드하니
`AddRepositoryPackage`가 매칭되는 파일이 없는 패키지를 에러 없이 조용히
빼버려서 -- 로컬 인덱스에 그냥 없는 것뿐 -- `haikuwebkit`이
`lib:libcrypto`를 요구하고 나서야 의존성 solver가 줄 게 없다는 게 드러났습니다.
`0014-minimum-webpositive.patch`에서 고쳤고, 거기서 WebPositive 자체와
HaikuWebKit, 그 밖의 런타임 의존성(sqlite3, dav1d, libavif1.0,
noto_sans_cjk_kr)도 같은 방식으로 선언합니다.

## QEMU 함정

취향이 아니라 실패를 직접 겪어서 정해진 것들:

- **부팅/설치 매체는 USB로 붙여야 합니다.** EDK2의 `edk2-aarch64` 펌웨어는
  SATA 드라이버가 없어서 AHCI 디스크는 펌웨어에 안 보이고 `map: No mapping
  found`로 UEFI Shell에 떨어집니다. Haiku 자체 드라이버는 커널이 돌고 나면
  AHCI 디스크를 잘 봅니다 -- 그걸로 시작을 못 하는 쪽은 펌웨어뿐입니다.
- **USB 저장장치 2개를 붙이면 부팅 스플래시에서 멈춥니다 -- 단, 두 번째
  것에 진짜 읽을 수 있는 파일시스템이 있을 때만요.** 빈/파티션 안 된 두
  번째 USB 디스크는 괜찮습니다(`--install`이 평소에 붙이는 게 바로 이건데,
  파티션하는 게 목적이니까요). 이미 설치된 디스크를 나중에 ISO와 함께 다시
  붙이면 이 행 증상이 재현됩니다. 설치된 디스크를 라이브 미디엄과
  비교/점검해야 할 때는 그 디스크를 `-device ahci`(그 밑에 `ide-hd`)로
  붙이세요 -- 부팅하고 나면 Haiku가 잘 봅니다.
- **QEMU를 끄기 전에 게스트를 정상적으로 종료하세요.** 이게 설치 절차에서
  진짜로 지켜야 할 단 하나입니다. 두 번 재현해서 확인했습니다: `Installer`가
  끝나자마자, 또는 EFI 로더를 복사하자마자 QEMU를 강제로 닫으면(QMP로
  `quit`이든 창을 그냥 닫든) Haiku의 쓰기 캐시가 디스크에 반영되지 않은
  채로 남습니다. 다음 부팅에서:
  - 복사된 `BOOTAA64.EFI`가 깨진 채로 올 수 있습니다(크기는 바이트 단위로
    같은데 MD5가 다름) -- EFI Shell에서 그걸 실행하면 `Command Error
    Status: Unsupported`가 납니다.
  - 시스템 패키지(실제로 본 사례: `zstd-1.5.6-2-arm64.hpkg`)도 같은 식으로
    깨질 수 있는데, `libbe.so`가 거기서 `libzstd.so.1`을 링크하므로
    `launch_daemon`이 시작을 못 하고 부팅이 스플래시 화면에서 멈춥니다.
    시리얼 로그엔 `runtime_loader: Cannot open file libzstd.so.1 ... error
    starting "/boot/system/servers/launch_daemon"`이 찍힙니다.

  둘 다 **복사가 깨진 증상이지, 패키징이나 packagefs 버그가 아닙니다** --
  같은 설치를 다시 하고 QEMU를 끄기 전에 Deskbar > Shutdown > Power off로
  종료했더니 바이트까지 동일한 파일(MD5로 확인)과 WebPositive까지 포함한
  완전한 데스크탑으로의 정상 부팅이 첫 시도에 나왔습니다. 게스트를 정상
  종료하면 QEMU도 알아서 꺼지니까(`arm64-patch` 0005의 PSCI 지원) 강제로
  닫을 이유가 애초에 없습니다.

## 알려진 한계

**WebPositive에서 HTML5 오디오·비디오가 전혀 재생되지 않습니다.**
HaikuWebKit의 `buildMediaEnginesVector()`
(`Source/WebCore/platform/graphics/MediaPlayer.cpp`)가 Cocoa, GStreamer,
Media Foundation, HolePunch 미디어 엔진은 등록하는데 Haiku용은 하나도 없습니다
-- `PlatformMediaEngineClassName`이 `PLATFORM(HAIKU)`에서 정의는 되는데
등록에 쓰이질 않아서 `installedMediaEngines()`가 비고 `canPlayType()`이
항상 빈 값을 돌려줍니다. HaikuWebKit 1.9.19, 1.9.26(x86)에서 확인됐고, 이
이미지는 같은 코드베이스의 1.10.0을 쓰므로 거의 확실히 같은 문제가 있을
텐데 여기서 직접 확인하지는 않았습니다. HaikuWebKit 소스 자체의 결함이라 --
고치려면 HaikuWebKit을 패치하고 `haikuwebkit`/`haikuwebkit_devel` 패키지를
다시 빌드해야 하는, 미리 빌드된 `haikuwebkit`을 그대로 쓰는 이 이미지와는
별개의 작업입니다. (병렬로 진행 중인 다른 세션이 x86용으로 고치고 있으니,
여기서 다시 조사하기 전에 그 수정이 1.10.0까지 반영됐는지 먼저 확인해볼
가치가 있습니다.)

## 확인된 것

- **라이브 부팅**(ISO가 유일한 USB 디스크로 붙은 상태): 데스크탑,
  WebPositive, `curl`/`wget`/`grep`/`tar`/`gzip`, 실제 HTTPS 요청
  (`curl -sI https://example.com` -> `200 OK`) 전부 정상 동작 확인.
- **설치**(DriveSetup -> Installer -> EFI 로더 복사 -> 정상 종료 -> 설치된
  디스크만 붙여서 재부팅): WebPositive까지 포함한 완전한 데스크탑으로
  부팅, 처음부터 끝까지 확인.
- **R\* 앱**: 부팅 후 호스팅된 저장소(https://pkgman.rainygirl.com/arm64)에서
  `pkgman`으로 설치 -- ISO에 미리 들어있진 않음.
