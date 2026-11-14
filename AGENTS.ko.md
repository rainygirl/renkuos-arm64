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
./run-qemu-renku-arm64.sh --bridged      # LAN에서 접근 가능한 NIC 추가(sudo 필요)
./run-qemu-renku-arm64.sh --clipboard    # 맥 클립보드를 게스트와 동기화
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

`arm64-patch/patches/`에는 RenkuOS/Source에 적용하는 패치 19개가 있고,
`build-renku-arm64-iso.sh`가 빌드할 때마다 자동으로 적용합니다. 각 패치가
정확히 뭘 바꾸고 왜 그런지는
[`arm64-patch/README.ko.md`](arm64-patch/README.ko.md)에 파일 단위로 자세히
적어뒀습니다. 여기서는 전체적인 모양만 간단히 설명합니다.

RenkuOS/Source의 `@minimum-mmc`/`@minimum-anyboot` 빌드 프로파일은
아키텍처와 상관없이 일부러 최소한의 기능만 담습니다. 미디어 스택도
없고, 브라우저도 없고, CLI 도구도 거의 없습니다. 패치는 크게 세 가지
일을 합니다:

1. 빠진 걸 다시 채웁니다 (0001, 0012, 0014).
2. arm64에서만 터지는 버그 세 개를 고칩니다: PL031 시계 (0005), 커널/runtime_loader/app_server 수정 (0006, 0007), packagefs가
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
  꺼지므로(RenkuOS/Source에 024aeb68a7부터 들어 있는 PSCI 전원 끄기 덕분입니다),
  애초에 강제로 닫을 이유가 없습니다.

- **화면이 뜨기도 전에 spurious-interrupt 폭주로 부팅이 멈출 수 있는데,
  이건 게스트가 아니라 호스트 쪽 문제입니다.** `virtio_pci: using legacy
  interrupt NN`와 `register_domain(4, link)` 직후에 시리얼 콘솔이
  `spurious interrupt (1023)` / `Last message repeated N번.`로 뒤덮이고
  게스트가 더 이상 진행하지 못합니다. 이 포트의 코드, 특정 디스크
  이미지나 ISO(한 번도 부팅한 적 없는 디스크 이미지와 ISO 사본에서도
  재현됨), 가속 방식(hvf와 `--tcg` 둘 다 걸림), GIC 버전, 붙어 있는
  장치 구성(네트워크/오디오 장치를 빼도, `virtio-gpu-pci`를 `ramfb`로
  바꿔서 커맨드라인에 virtio-PCI 장치가 하나도 없어도 재현됨) 때문이
  아님을 확인했습니다. 같은 세션에서 몇 시간 동안 수십 번 멀쩡하게
  부팅됐던 빌드와 호스트에서, 커맨드라인은 그대로인 채로도 재현됩니다 --
  즉 이 저장소나 게스트 내부에서 손댈 수 있는 문제가 아니라, QEMU/hvf를
  오래 켰다 `kill -9`로 끄는 걸 반복하면서 호스트 쪽 상태가 저하되는
  문제입니다. vCPU 수를 줄이면 이 레이스가 일어날 확률이 뚜렷하게
  낮아집니다(완전히 사라지지는 않습니다). `run-qemu-renku-arm64.sh`
  자체가 이제 시리얼 로그에서 이 패턴을 감지하는 부팅 워치독을 돌려서,
  멈추면 `-smp 2`로 한 번, `-smp 1`로 한 번 더 재시도한 뒤 그래도
  안 되면 조용히 멈추는 대신 명확한 에러를 내고 끝냅니다. `-smp 1`까지도
  안 풀리면(실제로 한 번 발생했고, 90초를 다 기다려도 그대로였습니다)
  호스트 자체를 재시작해야 합니다 -- 어떤 QEMU 플래그 조합으로도 그
  상태를 벗어나지 못했습니다.

  실제로 호스트를 그 지경까지 저하시킨 원인을 나중에 추적해보니: 한
  번의 디버깅 세션 동안 몇 시간 사이에 qemu-system-aarch64를 수십 번
  띄웠고, 그중 상당수를 정상 종료나 QMP `quit` 대신 `pkill -9`로
  끝냈습니다. 이 포트를 평소대로 쓰는 것 -- 한 번 부팅하고 게스트
  안에서 한 번 정상 종료하는 것 -- 은 이렇게 하지 않으니, 이건 평소
  사용에 대한 경고가 아니라 나중에 터미널에서 수동으로 부팅/강제종료를
  반복하며 뭔가를 디버깅할 사람(저 자신 포함)에게 남기는 메모입니다.
  그런 식으로 반복 작업을 해야 한다면, `kill -9`보다는 QMP `quit`을
  쓰거나 게스트가 스스로 끝나길 기다리고, 직전 시도가 끝나자마자 바로
  다음 시도를 붙여서 띄우지 마세요.

- **위에서 "호스트/hvf가 저하됐다"고 봤던 것 중 상당수는 실제로는
  고쳐진 진짜 버그였습니다: `0031-arm64-fix-icache-line-size-shift`.**
  `arch_cpu_sync_icache()`(업스트림 `src/system/kernel/arch/arm64/arch_cpu.cpp`,
  이 포트 자체 코드 아님)가 아이캐시 무효화 라인 크기를 구할 때
  `ctr_el0 & 0xF` 대신 `ctr_el0 << 0xF`를 썼습니다. QEMU가 게스트에게
  주는 정확한 `CTR_EL0` 비트 패턴에 따라 이게 라인 크기 0으로 계산될
  수 있는데, 그러면 바로 아래 무효화 루프가 절대 진행되지 않습니다 --
  모듈이나 드라이버가 코드 페이지를 쓰고 동기화하는 순간 CPU 100%로
  영원히 멈춥니다(QMP `info registers`로 멈춘 vCPU의 PC를 수십 번의
  부팅에 걸쳐 샘플링해서 매번 이 루프 안에 있는 걸 확인). 이게 위
  증상이 가속기 종류, GIC 버전, 디바이스 조합과 무관하게, 심지어 같은
  세션에서 방금 전까지 멀쩡히 부팅됐던 빌드에서도 재현된 이유입니다 --
  그 어느 것도 실제로는 중요하지 않았고, 두 번째 모듈을 로드할 만큼만
  부팅이 진행되면 오직 그 레지스터 하나의 비트 패턴에 따라 걸릴 수
  있었던 겁니다. 수정 완료; 패치를 적용해 재빌드한 뒤 예전엔 매번
  멈췄던 지점을 지나 FirstBootPrompt까지 반복해서 도달하는 걸 확인해
  고쳐졌음을 검증했습니다.

- **네트워크 장치가 있으면 데스크탑이 영원히 안 뜨던 문제 -- 수정됨
  (`0036-packagefs-lock-read-against-activation`).** 실제 NIC을
  붙이면(virtio-net-pci, e1000 둘 다 확인; hvf와 `--tcg` 둘 다에서 확인되어
  가속기 타이밍 문제는 아님) DHCP는 1밀리초 안에 완벽히 성공하는데도(QEMU
  자체의 `-object filter-dump` 패킷 캡처로 확인 -- DHCP 문제가 아니었음),
  커널과 `launch_daemon` 레벨에서는 부팅이 끝났습니다 -- `net_server`,
  `mount_server`, `package_daemon`, `syslog_daemon`, registrar(이 팀의
  메인 스레드 이름은 실제로 `roster`이지 `registrar`가 아닙니다 -- 스레드
  덤프에서 grep할 때 놓치기 쉬움)가 전부 뜨고 멀쩡히 유지됐지만 -- 그런데도
  데스크탑이 전혀 뜨지 않고 시스템은 그냥 유휴 상태로 있었습니다.

  `launch_daemon`/`autologin`/libroot의 `user_group_common.cpp`를 거쳐
  최종적으로 registrar의 `AuthenticationManager::_InitPasswdDB()` 자체까지
  들어가는 타겟 `debug_printf` 추적(전부 임시)으로 추적한 결과, 진짜
  원인은 `launch_daemon`도 `autologin`도 registrar 자체의 로직도 아니라
  packagefs 커널 코드의 진짜 버그였습니다:
  `packagefs_read()`(`src/add-ons/kernel/file_systems/packagefs/kernel_interface.cpp`)
  -- 일반 `read()` 시스템콜 전부, registrar 자신의 초기 `fopen("/etc/passwd")`도
  포함해서 그 뒤에 있는 진입점 -- 는 아무 락도 안 잡는데, `packagefs_read_stat()`,
  `packagefs_read_symlink()`, `packagefs_access()`는 전부 먼저 노드의 부모
  디렉토리에 `DirectoryReadLocker`를 잡습니다. 패키지 활성화
  (`Volume::_AddPackageContentRootNode()`가 바로 그 디렉토리의 *쓰기* 락
  아래서 호출하는 `UnpackingLeafNode::AddPackageNode()`/`RemovePackageNode()`)는
  언락된 읽기가 진행 중인 동안에도 어떤 노드를 뒷받침하는 `PackageNode`를
  바꿔칠 수 있습니다: `NodeReinitVFS()`는 새로 들어오는 `PackageNode`의
  `file_cache`를 먼저 초기화하고 나가는 쪽을 나중에 해제하므로, 그 사이에
  동시 읽기가 반쯤 해제 중이거나 반쯤 초기화 중인 `file_cache`에 걸려서
  0으로 채워진 페이지를 받아올 수 있는 구간이 생깁니다 -- 반면 같은 노드에
  대한 동시 *락이 걸린* `stat()`은 스왑 이후 resolve된 버전의 크기를
  보고하므로, 크기와 실제 데이터가 서로 어긋나게 됩니다. 직접 확인:
  `/etc/passwd`의 `fstat()`이 127바이트를 보고하는데 실제 읽은 내용은 전부
  0이고, 활성화가 끝난 뒤의 실제 파일은 43바이트의 고정된 내용
  ("baron:x:0:0:Root...")입니다. `autologin`, `media_server`, `sshd`가 각자
  자신의 초기 부팅 읽기에서 (크래시 없이, 평범한 에러 경로에서
  `EXIT_FAILURE`를 반환하며) 똑같이 실패하는 것도 같은 근본 원인입니다:
  셋 다 packagefs가 서빙하는 파일을 초기에 읽는데, 바로 그 시점에
  `package_daemon`이 패키지 활성화 작업을 반복해서 로그로 남기고 있습니다.
  네트워크 장치가 있어야만 안정적으로 재현된 건 네트워킹 자체가 관련된 게
  아니라, 추가된 드라이버/스레드 작업이 스케줄링을 충분히 흔들어서 이
  활성화 레이스가 읽기 도중에 걸릴 확률을 크게 높이기 때문이었습니다.

  첫 번째 수정 시도는 같은 `DirectoryReadLocker`를 `packagefs_io()`
  (direct/vectored I/O용 `io()` 진입점)에도 추가했습니다 -- 원리상 동일한
  위험이 있다고 판단해서였습니다. 실제로 빌드해서 부팅 테스트를 해 보니
  오히려 *더 나빠졌습니다*: 동일한 테스트 부팅 6번 중 6번 모두 정확히 같은
  손상이 재현됐습니다. 이 회귀의 원인: `packagefs_io()`는
  `packagefs_read()` 자신의 안에서도 같은 스레드로 *재진입* 호출됩니다 --
  `node->Read()` -> `PackageFile::Read()` -> `file_cache_read()` -> (방금
  `file_cache`가 생성된 노드라 거의 확실한 캐시 미스) -> `vfs_read_pages()`
  -> `vfs_vnode_io()`를 거쳐 같은 vnode에 대해 곧바로 `packagefs_io()`로
  다시 들어옵니다. Haiku의 `rw_lock`(`headers/private/kernel/lock.h`)은
  스레드별 재진입 추적이 없어서 -- 이미 읽기 락을 하나 들고 있는 스레드가
  같은 디렉토리에 두 번째 읽기 락을 시도하면, 바로 그 락을 기다리고 있는
  쓰기 측 뒤에 줄을 서게 되고, 그 쓰기 측은 바깥쪽(같은 스레드)의 읽기
  참조가 풀리기 전까진 진행할 수 없습니다 -- 자기 자신과의 교착
  상태입니다. 실제로 작동하는 수정은 `packagefs_read()`에만 락을 걸고
  (이 함수는 이미 `node->Read()` 호출 전체 동안 락을 들고 있어서,
  `packagefs_io()`로의 재진입 경로도 함께 보호됩니다) `packagefs_io()`
  자체는 락을 걸지 않고 둡니다(`packagefs_read()`를 거치지 않고 직접
  도달하는 호출 경로 -- 예: mmap 기반 I/O -- 는 이 레이스에 대해 원래부터
  보호받지 못하고 있던 것과 똑같은 상태일 뿐, 이 패치가 새로 만든 회귀는
  아닙니다). 수정 확인: 수정 전엔 6번 중 6번 모두 멈췄던 바로 그 부팅
  설정이, 수정 후엔 10번 중 10번 모두(진단 코드가 남아있던 6번 + 최종
  클린 빌드의 4번) 정상적으로 동작하는 `sshd`까지 도달했습니다.

  이건 이 포트 자신의 패치가 건드리는 영역이 아닌 업스트림 Haiku 커널
  코드이고, 패키지 활성화와 경쟁할 만큼 초기 읽기가 충분히 동시에 일어나는
  어떤 아키텍처의 Haiku 부팅에도 영향을 줄 수 있습니다 -- 업스트림에
  올릴 가치가 있습니다. `0033-media-server-addon-launch-retry`와
  `0034-autologin-startsession-retry`의 제한된 재시도는 (`0032`와 같은
  이유로) 무해한 방어막으로 그대로 남겨두지만, `0036`이 있으면 이 실패
  유형에 대해서는 더 이상 실제로 필요하지 않을 것입니다.

- **app_server가 첫 데스크탑 배경을 그릴 때 memcpy에서 세그폴트가 날 수
  있던 문제 -- 서로 다른 원인 두 가지, 둘 다 수정됨.** 시그니처(두 번 다
  동일): `debug_server: Thread N entered the debugger: Segment
  violation`, PC가 memcpy 안, `HWInterface::_CopyBackToFront` ->
  `CopyBackToFront` -> `InvalidateRegion` -> `DrawingEngine::FillRegion`
  -> `Desktop::_SetBackground` -> `Desktop::Init` ->
  `AppServer::_CreateDesktop`에서 호출됨.

  **원인 1, `0040-virtio-gpu-follow-host-size`:** `virtio_gpu_init_device()`
  (`src/add-ons/kernel/drivers/graphics/virtio/virtio_gpu.cpp`)는 "모든
  모드에 맞도록" 고정 크기(3840x2160, 4K) 프레임버퍼 영역 하나만
  할당했는데 — `bytes_per_row`/스캔아웃 리소스는 *실제* 요청된
  디스플레이 모드로 계산되고, `virtio_gpu_set_display_mode()`는 나중에
  모드가 바뀔 때마다 영역을 다시 할당하지 않고 같은 고정 영역을
  재사용했습니다. 4K를 넘는 모드는 이 영역을 넘겨썼습니다. Apple Silicon
  Mac Mini에서 실제 `system_profiler` 해상도인 6720x2836에서
  결정적으로(매 부팅마다, 간헐적이지 않게) 크래시하는 걸 확인했습니다.
  `0040`으로 고쳤습니다(`renkuos-apps/renku-retina`에서 가져온 것으로,
  거기 원래 수정은 5120x2880을 할당하는데 — 이 포트 자신의 QEMU 런처가
  게스트에게 호스트의 실제 해상도를 그대로 제공하고 6720x2836이 그걸
  넘어서기 때문에 그것만으로는 부족했습니다; 이 포트에서 추가한 부분은
  7680x4320을 먼저 시도하고 5120x2880, 3840x2160으로 차례로 떨어집니다).
  매번 크래시하던 바로 그 6720x2836 해상도로 다시 빌드·부팅해서 수정을
  확인했습니다.

  **원인 2, qemu-notch dylib의 `-S`/QMP `cont` 재개(`tools/qemu-notch/qemu-notch.m`):**
  이건 실제 맥북 에어의 실제 네이티브 해상도인 2560x1664에서의 두 번째
  재현을 설명하지 못했습니다 — 옛 4K 캡보다 훨씬 작아서 원인 1의 수정만
  으로는 여기에 도움이 안 됐습니다. 같은 해상도에서 통제된 A/B 부팅들로
  격리했습니다: `--window`(풀스크린 아님, dylib는 로드되지만 풀스크린/
  `-S` 로직이 한 번도 안 걸림)는 깨끗하게 부팅됐고, dylib를 완전히 끈
  `--fullscreen`도 깨끗했고, dylib의 EDID/Retina override는 켜둔 채
  테스트용으로 `-S` hold만 건너뛴 `--fullscreen`도 깨끗했습니다 — 실제,
  수정 없는 `--fullscreen` 경로(dylib 활성, `-S` hold, `cont` 전송)만
  크래시했고, 그것도 실제 기기에서 두 번 재현됐습니다. 근본 원인:
  `EnterOverlay()`가 뷰/창 크기를 바꾸고 `Refresh()`를 부르는데, 이건
  QEMU 자신의 Cocoa UI 스레드에게 새 크기를 적용하고 virtio-gpu의 EDID
  보고 지오메트리를 재협상하라고 *요청*만 할 뿐 — 그게 끝나길 기다리지
  않고 `StartGuest()`가 백그라운드 큐에서 QMP로 `cont`를 보냅니다. 바로
  그 틈에 실행을 시작한 게스트는 리사이즈가 실제로 적용되기 전에 자신의
  EDID 조회, 그리고 이어서 `Desktop::Init()`의 첫 페인트에 도달할 수
  있어서, 바로 밑에서 계속 바뀌고 있는 프레임버퍼 설정과 경쟁하게
  됩니다. `StartGuest()`가 QMP 소켓을 열기 전에 300ms `usleep`을 넣어
  고쳤습니다 — QEMU 자신의 (훅이 없는) 리사이즈 경로에 진짜 완료 콜백을
  연결하는 대신 고정된 지연을 쓴 것인데, 부팅이 이미 걸리는 몇 초나 풀
  스크린에 아예 도달하지 못했을 때 이미 허용하는 30초 폴백에 비하면
  무시할 만한 비용입니다. 수정 확인: 실제 크래시 해상도에서, 수정 없는
  `--fullscreen` 경로 그대로 5번 중 5번 성공했고, 실제 `MacRenkuOS.app`
  자체도 실제 대상 Mac에서 처음부터 끝까지 다시 띄워서 확인했습니다.

  둘 다 각각 업스트림/이 포트 자신의 툴링 문제입니다(virtio_gpu 쪽은
  업스트림 Haiku 커널 코드로 올릴 가치가 있고, qemu-notch 쪽은 Haiku
  코드가 아니라 이 포트 자신의 macOS 쪽 툴링입니다) — 같은 조사 과정에서
  드러났을 뿐, 위의 packagefs 수정과는 무관합니다.

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
- **브리지 네트워킹** (`--bridged`): 두 번째 NIC가 실제 LAN에서 DHCP로
  주소를 받고, 게스트의 NAT 서브넷 바깥에서도 sshd가 응답하는 것을
  확인했습니다. 이후 정상 종료까지 확인(sudo가 필요한 이유 등 자세한
  내용은 스크립트 자체의 헤더 주석 "Bridged networking" 참고).
- **클립보드 동기화** (`--clipboard`): `clipboard/clipboard-cli`를
  크로스빌드해서 양방향 왕복을 확인했습니다 -- 맥의 `pbcopy`가 게스트
  클립보드에 반영되는 것과, 게스트 클립보드가 `pbpaste`에 반영되는 것
  둘 다. 기존 SSH 포트를 통해 1초 간격으로 폴링합니다. 텍스트만
  지원합니다. Haiku에는 virtio-console이 아예 없어서(확인함, 추측
  아님) QEMU를 통한 직접 채널은 애초에 불가능합니다.
- **R\* 앱**: 부팅 후 호스팅된 저장소(https://pkgman.rainygirl.com/arm64)에서
  `pkgman`으로 설치합니다. ISO에 미리 들어있지는 않지만, 예외가 둘
  있습니다: R AppMarket 자체(0020) -- 나머지 앱을 설치할 GUI가 처음
  부팅부터 항상 있도록 -- 그리고 R Retina가 쓰는 app_server/Tracker/
  Screen/virtio_gpu 패치들(0037-0040)은 R Retina 자신의 `rretina`
  패키지에 맡기지 않고 이미지 자체에 바로 녹여 넣어서, 올바른 HiDPI
  스케일이 Installer가 열리기도 전인 첫 라이브 부팅부터 -- 나중에 패키지를
  설치한 뒤가 아니라 -- 이미 정해집니다.
- **레티나/HiDPI 감지** (0037-0040): 실제 대상 Mac(13인치 맥북 에어)에서
  실제 `MacRenkuOS.app` 실행 경로로 확인했습니다 -- `virtio_gpu_get_edid_info()`가
  패널을 2560x1664에서 29x18cm(≈224dpi)로 읽었고, `Desktop::_AutomaticUIScale()`가
  이를 2배로 바꾸는데, 그 패널에 정확히 맞는 스케일입니다 -- 크래시 없이
  정상 동작하는 `sshd`까지 완전히 부팅했고, 이건 위의 `0036`/qemu-notch
  지연 수정 전에는 크래시했던 바로 그 실제 실행 경로(notch dylib, 풀스크린,
  `-S`/QMP `cont`)입니다. virtio_gpu 프레임버퍼 오버플로우 수정이 회귀하지
  않았다는 것도 확인했습니다: 전에는 app_server를 결정적으로 크래시시키던
  바로 그 실제 `system_profiler` 해상도 6720x2836이 `0040`의 8K 우선
  할당 캐스케이드로 깨끗하게 부팅됩니다.
