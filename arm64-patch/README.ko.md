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


### 오디오 너머: 0005-0046

설치된 arm64 시스템에서 모든 앱을 시험하며 드러난 문제를, 이미지 안에서
우회하지 않고 이미지 자체에서 고친 것들입니다.

| 패치 | 무엇이 잘못돼 있었나 |
| --- | --- |
| `0005-arm64-pl031-rtc` | 매 부팅마다 시계가 1970년에서 시작해(PL031을 읽지 않음) 어떤 TLS 인증서도 유효하지 않았습니다. haiku-rwebpositive-arm64에서 가져와 맞췄습니다. 예전에는 PSCI 재부팅/전원 끄기도 넣었지만, RenkuOS/Source에 024aeb68a7부터 들어 있어 그 부분은 뺐습니다. 그 뒤 한 번 더 다시 만들었는데, 업스트림의 `2fe0ded501`이 같은 구조체의 똑같은 자리에 관련 없는 필드(`cacheable_memory_range`)를 추가했기 때문입니다 -- 같은 수정, 새 맥락일 뿐, `pl031_rtc`가 이제 그 바로 앞자리에 들어갑니다. |
| `0006-arm64-rchromium-fixes` | R Chromium에 필요한 커널·runtime_loader 수정이 별도 시스템 패키지에만 있었고, 그 패키지에는 미디어 스택이 없었습니다. 이제 이 이미지의 `haiku` 패키지에 들어 있고 `haiku_rchromium_fixes`를 제공합니다. TLS descriptor resolver는 ABI에 필요한 SIMD 전체 상태를 보존하고, relocation 인자는 이미지 언로드 때 해제됩니다. |
| `0007-app-server-layer-text` | haiku-rwebpositive-arm64의 수정: app_server 레이어 안 텍스트 경계. 예전에는 VMSAv8TranslationMap::Query의 페이지 테이블 조회 수정도 넣었지만, RenkuOS/Source에 b71ca76473부터 들어 있어 그 부분은 뺐습니다. |
| `0008-minimum-image-certs-and-repos` | 루트 인증서가 없었고, arm64에 존재하지 않는 HaikuPorts 저장소 설정이 있었으며, Haiku 자체 저장소의 더 새 `haiku` 패키지가 `pkgman update` 때 이 패치된 패키지를 덮어쓰게 돼 있었습니다. 이제 인증서가 들어가고 대신 RENKU Apps 저장소가 미리 설정됩니다. |
| `0009-arm64-zstd-feature` | packagefs가 zstd 패키지를 읽지 못했습니다. HaikuPorts 패키지 전부(인증서 포함)가 zstd입니다. `packages/`의 세 패키지가 필요합니다. |
| `0010-media-recorder-start-producer` | 녹음 앱이 사운드카드에 연결은 되지만 버퍼를 하나도 받지 못했습니다. BMediaRecorder가 생산자를 시작하지 않았습니다. |
| `0011-app-server-cjk-fallbacks` | "Noto Sans CJK JP"라는 글꼴이 없으면 한국어·일본어·중국어가 네모로 나왔는데, arm64에서는 그 글꼴을 설치할 수 없습니다. |
| `0012-minimum-cli-tools` | 새로 설치한 게스트는 pkgman 말고는 아무것도 받을 수 없었습니다. curl·wget·tar·gzip·grep·sed·python 이 없고 bash 에는 `/dev/tcp` 가 없습니다. grep·less·sed(부트스트랩 패키지)와 curl·wget·tar·gzip(pkgman-repo `scripts/cross-arm64-cli.sh` 로 크로스 빌드, hpkg 는 `packages/`)을 넣습니다. `HAIKU_NO_DOWNLOADS=1` 로 빌드합니다. |
| `0013-kernel-sock-nonblock` | `socket(SOCK_NONBLOCK)` 이 fd 에만 O_NONBLOCK 을 표시하고 소켓은 차단 모드로 남겼습니다. curl 은 매번 서버의 유휴 제한(30~400초)까지 `recv()` 에서 기다렸습니다. 이제 `fcntl(F_SETFL)` 처럼 스택에도 알립니다. |
| `0014-minimum-webpositive` | WebPositive가 이미지에 아예 없었습니다: `@minimum-mmc`는 절대 빌드하지 않고(브라우저 엔진 전체를 링크해야 하는 `haikuwebkit_devel`이 필요), `@minimum-anyboot`도 같은 정의를 물려받습니다. 0012가 curl/wget을 컴파일 대신 미리 빌드된 걸로 넣듯, 이미 빌드된 `webpositive` 패키지를 그대로 넣습니다. `packages/`의 `haikuwebkit`, `sqlite3`, `dav1d`, `libavif1.0`, `noto_sans_cjk_kr`도 함께 필요합니다. 또한 arm64 저장소 목록에 `openssl3`/`openssl3_devel`을 선언하는데, 이게 통째로 빠져 있었습니다: 0008이 이미 `AddHaikuImageSystemPackages`에 `openssl3`을 넣지만, 매칭되는 저장소 항목이 한 번도 선언된 적이 없어서 `HAIKU_NO_DOWNLOADS=1`에서 `AddRepositoryPackage`가 아무 에러 없이 조용히 빠뜨렸고 -- 로컬 인덱스에 그냥 없는 것뿐 -- 의존성 solver가 `haikuwebkit`의 `lib:libcrypto` 요구를 채울 게 없었습니다. 지금까지 됐던 건 어느 예전 빌드의 `download/`에 누군가 손으로 hpkg를 넣어뒀던 게 패치로 한 번도 캡처되지 않은 채 남아있었기 때문입니다. |
| `0015-arm64-webpositive-icu-data` | WebPositive가 실제 텍스트 레이아웃이 필요한 페이지(구글 홈페이지 등)를 열면 바로 죽었습니다: `WTF::TextBreakIteratorICU` 생성자가 ICU의 `ubrk_open()`이 실패하면 `RELEASE_ASSERT`를 겁니다. 빈/루트 로케일로도 실패했습니다. 원인: arm64의 부트스트랩 프로파일 `icu74` 패키지가 담고 있는 `libicudata.so.74`가 약 130KB짜리 스텁이라서, WebKit의 텍스트 분리에 필요한 약 30MB짜리 데이터셋이 빠져 있습니다 -- 그 전체 데이터셋은 `data/icu/74.1/icudt74l.dat`로 패키지 안에 같이 들어있지만 아무도 안 씁니다. 수정: `data/system/boot/SetupEnvironment`가 arm64에서 `ICU_DATA`를 그 경로로 export합니다 -- ICU 자체가 공식 지원하는 대체 경로 지정 방법입니다. |
| `0016-arm64-openssh` | arm64에는 `ssh`/`sshd`가 아예 없었습니다 -- HaikuPorts의 진짜(부트스트랩 아닌) arm64 저장소 자체가 비어있습니다. `packages/openssh-10.4p1-1-arm64.hpkg`는 0012의 curl/wget과 같은 방식으로, HaikuPorts 자체의 net-misc/openssh 패치셋을 써서 크로스 빌드했습니다. libedit은 뺐습니다(arm64 패키지 목록에 없고, sftp 줄 편집 기능에만 쓰임). `data/launch/sshd`(job/service 쌍, `data/system/boot/SshdKeygen`이 먼저 호스트 키를 만든 뒤)로 부팅 시 시작하며, 둘 다 `build/jam/packages/Haiku`에 새로 등록했습니다 -- `SetupEnvironment`/`data/launch/system`/`user`와 마찬가지로, `data/` 밑의 파일이 실제로 이미지에 들어가는 유일한 방법입니다. 서비스는 `launch`로 직접이 아니라 `/bin/sh -c "sshd -D"`로 감싸서 실행합니다 -- 취향이 아니라 실패를 겪어서 정해졌습니다: 직접 launch하면 sshd 자신의 디버그 로그엔 listening 중이라고 나오는데도 모든 연결을 거부하고, `sh -c` 한 겹만 거치면 매번 제대로 됩니다. launch_daemon 내부까지 원인을 찾지는 못했습니다. |
| `0017-tcp-timer-teardown` | 재전송 타이머가 이미 실행 중일 때 TCP 엔드포인트를 지우면 커널 패닉이 났습니다(`TCPEndpoint::_RetransmitTimer`에서 `_mutex_lock(): using uninitialized lock`, "net timer" 스레드). 소멸자가 타이머 훅을 기다리기 전에 엔드포인트의 락을 없앴기 때문입니다. 이제 엔드포인트에 표시를 하고 락을 푼 뒤 훅이 끝나기를 기다리고, 락은 마지막에 없앱니다. 업스트림 Haiku에도 같은 경합이 있습니다. 릴리스 커널에서는 드러나지 않고, 이 이미지의 KDEBUG 커널에서는 패닉이 납니다. 표시는 `FLAG_DELETING = 0x2000`입니다. RenkuOS/Source가 그 사이 0x800과 0x1000을 다른 플래그에 썼습니다. |
| *(0018 폐기)* | `0018-tcp-spawn-unbound`는 `_Spawn()` 패닉(`bound endpoint ... not in hash!`)을 고쳤는데, 지금은 업스트림의 `96d06c2e46`("tcp: _Spawn() resets local address", 메인라인 Haiku에서 체리픽)가 똑같이 고쳐줍니다 -- 완전히 같은 한 줄짜리 수정, 같은 파일, 같은 자리. 0005/0007처럼 빈 채로 남기지 않고 통째로 없앴습니다: 더 이상 이 프로젝트만의 것이 아니기 때문입니다. 번호가 비는 건 의도한 것이고, 뒤 번호를 전부 당기지는 않았습니다. |
| `0019-net-notifications-keep-loaded` | 부팅이 교착될 수 있었습니다. net_server가 네트워크 알림 모듈을 내리면서 모듈 락을 쥔 채 `unregister_generic_syscall()`에서 진행 중인 `net_notifications_control()` 호출을 기다렸습니다. 그 호출(launch_daemon의 리스너 추가)은 `FirstAdded()` -> `get_module()`에서 같은 모듈 락을 기다리고 있었습니다. 이제 모듈은 `B_KEEP_LOADED`이고, `FirstAdded()`/`LastRemoved()`는 `get_module()`/`put_module()`을 부르지 않습니다. x86_64 부팅 14회 중 1회 발생했습니다. |
| `0020-minimum-rappmarket` | R AppMarket(창 하나로 등록된 각 pkgman 저장소의 모든 앱을 찾아보고 설치)을 쓰려면 먼저 손으로 `pkgman install`부터 해야 했습니다 -- RENKU Apps의 나머지를 설치하는 데 가장 먼저 필요한 바로 그 앱인데도요. 0012/0016이 빌드된 curl/openssh를 올리는 것과 같은 방식으로 패키지를 올립니다: `packages/rappmarket-1.0.0-4-arm64.hpkg`를 arm64 저장소 목록과 `AddHaikuImageSystemPackages`에 선언했습니다. |
| `0021-arm64-libroot-swap-float` | `__swap_float`/`__swap_double`(`B_HOST_TO_BENDIAN_FLOAT` 등)의 `fmov` 피연산자 순서가 반대라 엉뚱한 값을 돌려줬습니다: `__swap_float(1599.0f)`가 `00e0c744`가 아니라 `a73ef000`. 모든 이미지 트랜슬레이터가 비트맵 헤더의 bounds를 이것으로 쓰기 때문에 BBitmapStream이 엉뚱한 크기의 비트맵을 만들고 넘치게 썼습니다 -- JPEG 배경화면을 그리던 Tracker와 JPEG를 열던 ShowImage가 죽었습니다. 고친 명령을 게스트에서 float, double, 왕복으로 확인했습니다. |
| `0022-minimum-image-translators` | minimum 프로필에는 STXTTranslator 하나뿐이라 ShowImage가 아무것도 열지 못했고 Backgrounds에서 고른 JPEG도 그려지지 않았습니다. `packages/renku_imagetranslators-1.0.0-1-arm64.hpkg`(AVIF, BMP, GIF, HVIF, ICO, JPEG, PCX, PNG, PPM, PSD, RAW, RTF, SGI, TGA, WebP, WonderBrush)를 추가합니다. arm64 HaikuPorts에 libjpeg/libpng/libwebp/libavif -devel 패키지가 없어 미리 빌드했고, 이미지에 이미 있는 haikuwebkit과 libavif1.0의 라이브러리에 링크합니다. 빌드 방법: `imagetranslators/`. |
| `0023-arm64-kernel-empty-translation-map` | `fork()`가 실패하면 시스템이 패닉했습니다. 사용자 `VMSAv8TranslationMap`은 첫 `Map()` 때 페이지 테이블을 만드는데, 그 전에 지워지면(메모리 부족으로 `fork_team()`이 포기할 때) `fPageTable`이 0인데도 소멸자가 해제하려고 물리 페이지 0을 훑었습니다(`fork_team`에서 부른 `~VMSAv8TranslationMap`에서 `unhandled page fault in kernel space at 0xffffff0000000000`). 4GB VM에서 메모리가 부족할 때 sshd와 R Chromium에서 두 번 일어났습니다. |
| `0024-libbe-app-launched-by-exec` | 스크립트가 `exec()`로 실행하는 앱(R Chromium, 웹앱 런처)이 CPU 코어 하나를 계속 썼습니다. roster가 실행한 팀의 루퍼 포트를 만들어 주는데 `exec()`가 그 팀의 포트를 지우고, `BApplication`은 지워진 포트를 그대로 받아 써서 `task_looper()`가 `B_BAD_PORT_ID`로 공회전했습니다. 시작 메시지도 같이 사라져 `ReadyToRun()`도 불리지 않았습니다(이렇게 실행한 DeskCalc는 창이 뜨지 않음). 이제 roster의 포트가 살아 있고 그 팀 소유일 때만 받아 쓰고, 아니면 자기 포트를 쓰면서 `B_ARGV_RECEIVED`와 `B_READY_TO_RUN`을 스스로 보냅니다. |
| `0025-usb-disk-recover-after-timeout` | VM이 `PANIC: last transaction (853) still open!`으로 커널 디버거에 멈췄습니다. USB 디스크 전송이 10초를 넘기면(호스트가 백그라운드이거나 바빠서 QEMU를 돌리지 못하는 동안 게스트 시계는 계속 감) Bulk-Only 리셋 없이 취소되었고, 장치가 그 뒤 모든 명령에 Stall로 답했으며, 블록 읽기/쓰기는 재시도하지 않아 BFS가 로그를 쓰지 못했습니다. 이제 실패한 전송 뒤에는 항상 리셋하고 READ/WRITE를 네 번까지 재시도합니다. |
| `0026-vm-free-shadowed-modified-pages` | 메모리가 부족할 때 sshd의 `exec()`에서 `ASSERT FAILED (vm_page.cpp:3172): (page->State() != PAGE_STATE_MODIFIED)`로 VM이 커널 디버거에 멈췄습니다. 업스트림의 장치별 modified 큐 도입 뒤로 MODIFIED 페이지는 캐시에서 빠진 다음에는 해제할 수 없는데, 가려진 페이지를 해제하는 익명 캐시 병합 경로 두 곳(`_MergePagesSmallerConsumer`, `_MergeSwapPages`)이 `_FreePageRange()`와 `Delete()`처럼 먼저 CACHED로 옮기지 않았습니다. 이제 옮깁니다. [RenkuOS/Source#343](https://github.com/RenkuOS/Source/pull/343)으로 제출했습니다. |
| `0027-vfs-unlock-io-context-before-free` | 메모리가 부족할 때 R Chromium의 `fork()`에서 `acquire_spinlock(): Failed to acquire spinlock ... (value: deadbeef)`로 VM이 커널 디버거에 멈췄습니다. upstream c02707824b 이후 `vfs_new_io_context()`는 새 컨텍스트의 락을 `WriteLocker`로 잡아 두는데, FD 테이블 할당에 실패하면 컨텍스트를 해제하고 반환해서 그 뒤 락커가 해제된 메모리의 락을 풀었습니다. 이제 락을 먼저 풀고 없앤 뒤 해제합니다. |
| `0031-arm64-fix-icache-line-size-shift` | 부팅이 모듈/드라이버를 로드하는 지점마다(처음엔 네트워크 드라이버 로딩에서 재현됐지만 그것만의 문제는 아님) CPU 100%로 영원히 멈출 수 있었습니다. `arch_cpu_sync_icache()`가 `CTR_EL0`에서 아이캐시 라인 크기를 구할 때 `ctr_el0 & 0xF`(하위 4비트 마스킹) 대신 `ctr_el0 << 0xF`(레지스터 전체를 왼쪽으로 15비트 시프트)를 썼습니다 — 바로 아래 `dcache_line_size`는 이걸 제대로 합니다. `4 << (거대한 시프트량)`은 C++에서 정의되지 않은 동작이고, 라인 크기가 0으로 계산될 수 있는데, 그러면 바로 아래 무효화 루프가 절대 진행되지 않는 무한 루프가 됩니다. QEMU/hvf에서 멈춘 vCPU의 PC를 샘플링해서 이 함수의 무효화 루프 안에 계속 머무는 걸 확인했습니다. 업스트림 Haiku arm64 코드(`6bcb56f62dfd20138ddc4194972f6eec7101800d`에서 도입)로, 이 포트만의 문제가 아니며 확인되면 업스트림에 올릴 가치가 있습니다. |
| `0032-launchdaemon-restart-throttle` | 모든 실행 시도 직후 바로 종료되는 `service` 잡(QEMU/hvf에서 virtio-net-pci를 붙였을 때 `media_server`, 그리고 별개로 `sshd`에서 관찰됨 — 아래 "네트워크 장치가 있으면 데스크탑이 영원히 안 뜨던 문제" 참고)이 `B_SYSTEM_OBJECT_UPDATE`의 `B_TEAM_DELETED` 핸들러에 의해 커널이 team을 만들고 없앨 수 있는 최대 속도로 재시작되고 있었습니다 — 커널 스레드 덤프로 확인: 해당 잡의 team ID가 5초마다 약 3000씩 올라가는데 세마포어/포트/스레드/팀/가용 메모리는 전부 평평했습니다(누수 없이 그냥 스로틀 없는 루프). 핸들러 자체의 `// TODO: take restart throttle into account` 주석이 이미 이 공백을 지적하고 있었습니다. 이제 각 `Job`이 마지막 실행 시각을 기억하고, 그로부터 1초 이내에 team이 죽으면 즉시가 아니라 일회성 `BMessageRunner`를 통해 재시작합니다. 업스트림 `src/servers/launch/` 코드로 이 포트만의 것이 아니며, 근본적인 재시작 원인이 무엇이든 상관없이 업스트림에 올릴 가치가 있습니다. |
| `0033-media-server-addon-launch-retry` | `media_server`가 `media_addon_server`를 실행하려는 시도(signature로 찾고, 안 되면 경로로) 한 번이 실패하면 바로 `exit(1)`했습니다. `launch_daemon`의 종료 코드 로깅으로 확인: 데스크탑까지 도달하지 못하는 부팅(NIC 연결 시)에서는 1, 도달하는 부팅에서는 0. 이제 signature-then-path 전체 시퀀스를 100ms 간격으로 최대 20번 재시도합니다 — 그 자체로 제대로 된, 범위가 잘 제한된 견고성 수정이지만, 이것만으로는 근본 문제를 고치지 못한다고 확인했습니다(아래 참고). |
| `0034-autologin-startsession-retry` | `autologin`은 일회성 `job`(`service`가 아님)이라서, `getpwuid(0)`이나 `BLaunchRoster().StartSession()` 호출 한 번만 실패해도 유저 세션이 없고, 따라서 데스크탑도 영원히 없습니다 — 부팅의 다른 부분은 멀쩍이 정상으로 보이는데도요. 같은 종료 코드 로깅으로 확인했고, 한 단계 더 추적했습니다: 이 상황에서는 `getpwuid(0)`이 3초에 걸친 30번의 재시도 전부에서 `NULL`을 반환합니다 — 짧은 레이스가 아니라 지속적인 실패입니다. 이제 두 호출을 묶어서 몇 초간 재시도한 뒤 포기합니다 — `0033`과 같은 이유이고, 마찬가지로 그것만으로는 충분하지 않다고 확인했습니다: `getpwuid(0)`이 애초에 계속 실패했던 진짜 원인은 바로 아래에서 찾아 고쳤습니다. |
| `0036-packagefs-lock-read-against-activation` | `0032`/`0033`/`0034`의 증상들 뒤에 있던 진짜 근본 원인입니다: `packagefs_read()`(`src/add-ons/kernel/file_systems/packagefs/kernel_interface.cpp`) — 일반 `read()` 전부, registrar 자신의 초기 `fopen("/etc/passwd")`도 포함해서 그 뒤에 있는 진입점 — 는 `packagefs_read_stat()`/`read_symlink()`/`access()`와 달리 아무 락도 잡지 않았습니다. 동시에 일어나는 패키지 활성화(`AddPackageNode()`/`RemovePackageNode()`, 부모 디렉토리의 쓰기 락 아래서 실행)가 읽기 도중에 노드의 `PackageNode`를 바꿔칠 수 있어서, 언락된 읽기가 반쯤 해제/초기화 중인 `file_cache`에 걸려 0으로 채워진 페이지를 받아올 수 있었고, 동시에 락이 걸린 `stat()`은 스왑 이후 버전의 크기를 보고했습니다 — 직접 확인: `fstat()`이 127바이트를 보고하는데 실제 내용은 전부 0, 실제 파일은 고정된 43바이트. 이제 `packagefs_read()`에 `DirectoryReadLocker`를 잡습니다(형제 함수들과 동일) — 단, 원리상 같은 위험이 있는 `packagefs_io()`에는 *일부러* 걸지 않습니다: `packagefs_io()`는 `packagefs_read()` 자신의 안에서도 같은 스레드로 재진입 호출되는데(`node->Read()` -> `file_cache_read()` -> 캐시 미스 -> `vfs_read_pages()` -> `vfs_vnode_io()`가 다시 `packagefs_io()`로), Haiku의 `rw_lock`은 스레드별 재진입 추적이 없어서 둘 다 잠그면 쓰기 측이 대기 중일 때 자기 자신과 교착합니다 — 실제로 그 버전을 빌드해서 부팅 테스트해보니 동일 테스트 6번 중 6번 모두 재현됐습니다. `packagefs_read()`의 락은 `node->Read()` 호출 전체를 감싸므로 `packagefs_io()`로의 재진입 경로도 이미 보호합니다. 수정 확인: 수정 전 6번 중 6번 멈췄던 바로 그 설정이 수정 후 10번 중 10번 모두 정상 동작하는 `sshd`까지 도달했습니다. 이 포트만의 것이 아닌 업스트림 Haiku 커널 코드로, 업스트림에 올릴 가치가 있습니다. |

| `0037-app-server-dpi-ui-scale` | 글꼴 크기를 고르지 않았을 때 app_server는 해상도만으로 UI 스케일을 골랐습니다(1920x1080 넘으면 1.5배, 3840x2160 넘으면 2배) — 13인치 2560x1664 노트북 패널과 27인치 2560x1440 모니터가 픽셀 밀도는 전혀 다른데도 같은 스케일을 받았습니다. 이제 EDID에서 패널의 실제 물리 크기를 읽어 `dpi / 110`을 1~3 사이 0.25 단위로 씁니다. EDID가 없으면 예전 해상도 규칙을 그대로 씁니다. [renkuos-apps/renku-retina](../../renkuos-apps/renku-retina)(원래는 `rretina` 패키지로 나중에 `pkgman install`하는 방식, 그 프로젝트의 패치 0001)에서 가져왔습니다 — 이미지 자체에 녹여 넣어서, 나중에 패키지를 설치한 뒤가 아니라 Installer가 열리기도 전인 첫 라이브 부팅부터 이미 올바른 스케일이 정해집니다. 실제 대상 Mac에서 확인: EDID가 2560x1664에서 29x18cm(≈224dpi)로 읽혀 2배를 고르는데, 그 패널에 정확히 맞는 값입니다. |
| `0038-tracker-desktop-location-scale` | 데스크탑 아이콘 위치는 픽셀로 저장되어서, UI 스케일이 바뀌면(HiDPI 디스플레이에서 더 큰 기본 글꼴이 되거나, 사용자가 글꼴 크기를 고르거나) 아이콘과 그 라벨들이 서로 겹칩니다. 이제 Tracker가 위치를 저장할 때의 스케일을 데스크탑 디렉토리의 속성(`_trk/pinfo_scale`)으로 기록하고, 불러올 때 현재 스케일로 다시 비례시킵니다. renku-retina(패치 0002)에서 가져왔습니다 — 이미지에 녹여 넣은 이유는 `0037` 참고. |
| `0039-ui-scale-setting` | Screen 설정에 "UI scale" 메뉴(Automatic, 100%, 150%, 200%)를 추가합니다 — 그전엔 손으로 고른 네 개의 글꼴 크기뿐이라, 한 번 고르면 다음에 어떤 디스플레이로 부팅하든 그대로 고정됐습니다. "Automatic"은 `~/config/settings/system/app_server/ui_scale`에 `auto`를 남겨서, 저장된 크기가 덮어쓰는 게 아니라 `0037`의 EDID 기반 결정이 매 부팅마다 다시 실행되게 합니다. renku-retina(패치 0003)에서 가져왔습니다 — 이미지에 녹여 넣은 이유는 `0037` 참고. |
| `0040-virtio-gpu-follow-host-size` | 유저랜드 헬퍼가 아무것도 건드리지 않고 호스트(QEMU)의 현재 창 크기를 물어볼 수 있는 `VIRTIO_GPU_GET_PREFERRED_SIZE` ioctl을 추가하고, 두 번째 프로세스가 장치를 리셋 없이 열 수 있게 하며, 이미 놓친 완료를 영원히 기다릴 수 있던 명령 완료 대기 버그(간헐적으로 화면이 검게 나옴, x86_64에서 5번 중 2번 확인)를 고칩니다. 프레임버퍼는 이제 contiguous 메모리를 구할 수 있는 가장 큰 모드로 할당됩니다 — 순서대로 7680x4320(8K), 5120x2880(5K), 3840x2160(4K)을 시도하고 — `set_display_mode()`는 담을 수 없는 모드를 조용히 넘겨쓰는 대신 거부합니다. `0037` 시기 테스트에서 Apple Silicon Mac Mini의 실제 `system_profiler` 해상도인 6720x2836에서 app_server가 매번(결정적으로) 크래시하는 걸 확인했는데, 이는 원래 업스트림이 고른 5K만으로는 충분하지 않았기 때문입니다 — 이 포트 자신의 QEMU 런처가 게스트에게 호스트 해상도를 그대로 제공하기 때문입니다. 8K를 먼저 시도하는 캐스케이드로 바로 그 해상도에서 수정을 확인했습니다. renku-retina(패치 0004, 이게 가능하게 하는 EDID 재조회도 함께 포함 — `0037` 참고)에서 가져왔고, 프레임버퍼 캐스케이드는 이 포트에서 추가한 부분입니다. |
| `0041-arm64-auto-install-rchromium` | 설치 직후에는 R AppMarket을 열거나 `pkgman`을 직접 실행하기 전까지 브라우저가 전혀 없었습니다. `data/launch/user`의 `desktop` 타겟에 `renku-install-rchromium` job을 추가합니다: `create-installer-link`가 이미 실제 설치와 라이브/설치 매체를 구분하는 데 쓰는 `not read_only` 조건으로 걸어두고(라이브 매체의 쓰기 영역은 브라우저 패키지를 담기엔 너무 작아서, 거기서 실행하면 바로 pkgman이 내는 "No space left on device" 오류가 남), `update-time`이 이미 쓰는 `on network_available`로 트리거하며, job 자신의 스크립트(`data/system/boot/renku-install-rchromium`)가 `pkgman install -y rchromium`이 실제로 성공했을 때만 쓰는 마커로 게이트합니다 — 아직 네트워크가 안 올라왔거나 서버에 닿지 않는 부팅이면 다음 부팅에서 그냥 다시 시도합니다. 기존 `first-login`/`PostInstallScript` 메커니즘은 일부러 쓰지 않았습니다: 그 마커는 스크립트 성공 여부와 무관하게 무조건 지워지므로, 딱 한 번만 시도할 기회를 줄 뿐입니다. |
| `0042-live-image-rchromium` | 위의 USB 설치 매체는 여전히 네트워크가 있는 실제 부팅이 한 번 있어야 R Chromium이 생깁니다. [MacRenkuOS.app](../macapp/)이 실제로 내려받아 설치에 쓰는 라이브 매체는 이미지에 바로 올려서 포함시킵니다 — 같은 `build-renku-arm64-iso.sh` 트리를 다른 방식으로 한 번 더 빌드하는 것뿐입니다: `RENKU_LIVE_BUILD=1`이면 `rchromium-154.0.8036.0-31`을 `AddHaikuImageSystemPackages`에 추가하는데, `DefaultBuildProfiles`에서 이 jam 변수로 게이트해 뒀기 때문에 일반 설치용 빌드는 이걸 요청하지 않습니다. 그 전에 재벤더링이 필요했습니다: `package_repo`는 이 로컬 HaikuPorts-info 레포지토리의 나머지와 vendor가 다른 패키지를 거부하는데, 이는 `0012`의 curl/wget과 `0020`의 rappmarket이 이미 부딛힌 것과 똑같은 제약이고, 거기서는 각자의 레시피에서 vendor를 "Haiku Project"로 강제해서 다시 빌드하는 식으로 피해갔습니다 — 하지만 몇 시간짜리 네이티브 Chromium 빌드로 똑같이 하는 건 현실적이지 않습니다. 대신 `build-renku-arm64-iso.sh`는 이미 빌드된 hpkg를 재벤더링합니다: 호스트용 `package` 툴(`<build>package`, jam이 다른 호스트용 빌드 툴을 만드는 것과 같은 방식)을 빌드하고, 내려받은 hpkg를 추출해서, extract가 써 주는 `.PackageInfo`의 vendor 줄만 고친 뒤, 그 수정된 사본으로 다시 패키징합니다 — 바이너리는 그대로고 메타데이터만 다릅니다. |
| `0043-live-image-no-installer` | 라이브 매체(`RENKU_LIVE_BUILD=1`)는 매체에서 바로 실행되는 게 전제라서 — 거기선 설치할 게 없습니다. 같은 jam 변수로 게이트해서 `Installer`를 `SYSTEM_APPS`와 `DESKBAR_APPLICATIONS` 양쪽에서 걸러냅니다. `data/launch/user`의 `create-installer-link` job(읽기전용/라이브 부팅에서 데스크탑에 Installer 바로가기를 만드는 job)은 이미 `file_exists /boot/system/apps/Installer`로 게이트되어 있어서, 바이너리를 빼는 것만으로도 그 바로가기가 안 생기게 하는 데 충분합니다 — 거기는 따로 손댈 게 없습니다. |
| `0044-live-image-skip-firstboot` | `0043`으로도 FirstBootPrompt의 Install/Try it out 화면 자체는 매번 라이브 부팅 때마다 그대로 떴습니다(`data/launch/user`의 `run` job은 매체가 읽기전용인지로 이 화면을 띄울지 정하는데, 라이브 매체는 항상 읽기전용입니다) — 바이너리가 사라졌으니 "Install"은 눌러도 실패하는 죽은 버튼이 되고요(`FirstBootPromptWindow`는 Installer가 실제로 있는지 확인하지 않고 그 버튼을 무조건 그립니다). `run`의 조건은 `/system/bin/FirstBootPrompt`가 있을 때만 `first_boot`로 들어가므로, `RENKU_LIVE_BUILD=1`일 때 `SYSTEM_BIN`에서 그걸 걸러내는 것만으로 — `0043`과 같은 방식 — `data/launch/user`를 따로 안 고쳐도 라이브 매체가 바로 데스크탑으로 부팅되게 할 수 있습니다. |
| `0045-boot-splash-logo` | `app_server`가 뜨기 전까지는 QEMU 창에 보여줄 게 없습니다 — EDK2/EFI 부트로더가 virtio-gpu에 아직 아무것도 그리지 않은 상태라, 그동안은 cocoa 디스플레이 자신의 "Display output is not active" 플레이스홀더만 보입니다. RenkuOS/Source 자신의 부트로더는 이미 그 공백 구간에 로고를 그리게 돼 있지만(`video_display_splash()`, `default` 빌드에 이미 연결돼 있음) 372x96 이미지가 비어있는 채로 컴파일돼 있었습니다 — 상표 미포함 빌드에서 Haiku 로고를 뺀 뒤 RenkuOS 것으로 대체하지 않았기 때문입니다. [`renkuos-logopatch`](../renkuos-logopatch/)에서 그대로 가져왔습니다(같은 고정 커밋 기준으로 생성됨): `headers/private/kernel/boot/images.h`의 고대비 RenkuOS 워드마크만 바꾸고 나머지는 그대로입니다. arm64 전용 수정은 아닙니다 — 업스트림에서는 어느 아키텍처든 이렇게 비어있습니다 — 하지만 이 이미지의 부팅 과정 중 RenkuOS 브랜딩이 전혀 안 보이는 유일한 순간이라 포함해 둘 가치가 있습니다. |
| `0046-live-disk-grow-boot-volume` | 라이브 매체의 `/boot`는 이미지 안의 300 MiB BFS 파티션 그대로라, 바깥 디스크가 아무리 커도 300 MiB(여유 약 10 MiB)에 머물렀습니다. R Chromium의 공유 메모리가 이 볼륨의 `/tmp`에 만들어져 큰 화면 한 장에도 모자랄 정도였습니다. 이미지를 키우면 그만큼 다운로드가 커지므로, `run-qemu-renku-arm64.sh --live`가 qcow2 overlay의 가상 크기를 16 GiB로 잡고(쓰기 전에는 공간을 차지하지 않음) 게스트가 그 안으로 커집니다. 첫 로그인 때 `data/launch/user`의 `renku-grow-boot` 작업이 새 도구 `renku_grow_boot --restart`(`src/bin`)를 실행합니다. 라이브 디스크 구조(ISO 9660 하이브리드, MBR에 BFS와 그 바로 뒤 ESP만 있음)일 때만 ESP를 디스크 끝으로 복사·검증하고, BFS 파티션을 그 앞까지 늘리고, `~/config/settings/kernel/drivers/bfs`에 `grow_to_partition true`를 쓴 뒤 안내창을 띄우고 재시작합니다. 재시작 때 BFS의 `Volume::Mount()`가 저널·캐시·할당기를 시작하기 전에 파일 시스템을 키웁니다(`_GrowToPartition`). BFS는 원래 크기를 바꿀 수 없습니다(`ResizeVisitor`는 껍데기). BFS는 저널 끝까지를 예약 영역으로 보므로 저널 끝은 그대로 두고, 커진 비트맵이 저널 앞부분을 가져갑니다(16 GiB 기준 2048 -> 1546블록). 다른 데이터는 옮기지 않습니다. 저널이 깨끗해야 하며, 예상과 다른 구조면 볼륨을 그대로 둡니다. 새 16 GiB overlay에서 확인: 첫 로그인 안내창, `bfs: grew "Haiku" from 153600 to 8385024 blocks`, `/boot` 16.0 GiB, `checkfs` 정상. 하면서 찾은 것: minimum 이미지 정의 끝에서 `system boot`에 넣는 파일은 이 이미지에 들어가지 않습니다. 0041의 `renku-install-rchromium` 스크립트가 이미지에 없는 것도 같은 이유입니다. |

## 어떤 패치가 RenkuOS/Source PR 후보인가

이 패치 세트 대부분은 이 포트만의 것입니다: arm64용 `@minimum-anyboot` 빌드
프로파일 설정, 패키지 목록, 레포지토리 정의처럼 이 이미지에만 해당하는
패키징입니다. 업스트림에 보낼 만한 게 아닙니다. 소수의 패치는 다릅니다:
RenkuOS/Source 자체 코드의 실제 버그를 고친 것으로, 이 포트가 arm64와
QEMU/hvf를 보통보다 더 세게 굴려서 발견됐을 뿐이고, 이 포트가 있든 없든
업스트림에 들어가야 할 수정입니다. PR 준비를 위해 따로 뽑았습니다:

**이미 업스트림에 보낸 것:**

| 패치 | PR |
| --- | --- |
| `0026-vm-free-shadowed-modified-pages` | [RenkuOS/Source#343](https://github.com/RenkuOS/Source/pull/343) |

**위 설명에서 이미 업스트림 대상이라고 적어뒀지만 아직 안 보낸 것:**

| 패치 | 버그가 실제로 있는 곳 |
| --- | --- |
| `0017-tcp-timer-teardown` | `src/system/kernel/...` TCP 엔드포인트 해제가 자기 재전송 타이머와 경쟁 — 아키텍처 무관, 릴리스 커널 빌드라면 어디서나. |
| `0031-arm64-fix-icache-line-size-shift` | `arch_cpu_sync_icache()`, `src/system/kernel/arch/arm64/arch_cpu.cpp` — arm64 부팅이면 어디서나, 이 포트만의 문제 아님. |
| `0032-launchdaemon-restart-throttle` | `src/servers/launch/` — launch 직후 종료하는 `service` job이면 아키텍처 무관, 핸들러 자신의 `// TODO: take restart throttle into account` 주석이 이미 이 구멍을 가리키고 있었음. |
| `0036-packagefs-lock-read-against-activation` | `src/add-ons/kernel/file_systems/packagefs/kernel_interface.cpp` — `packagefs_read()`의 락 누락, 패키지 활성화와 경쟁하는 동시 읽기가 충분하면 어느 아키텍처에서도 재현 가능. |

**같은 종류의 진짜, 포트 고유가 아닌 버그로 보이지만 업스트림을 염두에
두고 따로 검토한 적은 없는 것 — 보내기 전에 한 번 더 볼 가치가 있음:**

| 패치 | 위치 |
| --- | --- |
| `0019-net-notifications-keep-loaded` | `net_server`의 모듈 락과 `net_notifications_control()` 간의 교착 — arm64 고유 아닌 일반적인 버그. |
| `0021-arm64-libroot-swap-float` | libroot의 `__swap_float`/`__swap_double` — arm64 고유(`fmov` 오퍼랜드 문제)지만, 어느 arm64 빌드에서나 나는 libroot 버그이고 이 포트만의 것은 아님. |
| `0023-arm64-kernel-empty-translation-map` | 한 번도 할당된 적 없는 페이지 테이블을 해제하는 `~VMSAv8TranslationMap` — arm64 고유, 이 포트만의 것 아님. |
| `0024-libbe-app-launched-by-exec` | `BApplication`/roster의 looper 포트를 `exec()` 너머로 넘기는 부분 — 일반 libbe, 아키텍처 무관. |
| `0025-usb-disk-recover-after-timeout` | `usb_disk`가 타임아웃된 전송 후 Bulk-Only 리셋과 재시도를 안 하던 부분 — 일반 USB 드라이버 코드, 아키텍처 무관. |
| `0027-vfs-unlock-io-context-before-free` | `vfs_new_io_context()`가 할당 실패 시 해제된 메모리를 언락하는 부분(업스트림 `c02707824b`에 뿌리를 둠) — 일반 VFS 코드, 아키텍처 무관. |

위 표의 나머지는 전부 이 포트만의 것입니다: arm64용으로 줄인
`@minimum-anyboot` 이미지라서만 의미가 있는 빌드 프로파일 작업과
패키징, 설정입니다.

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

빌드 스크립트는 이제 RenkuOS/Source의 커밋
[`8484655099aae479b59590d098dfcbf6fdc4d908`](https://github.com/RenkuOS/Source/commit/8484655099aae479b59590d098dfcbf6fdc4d908)
(2026-11-06)을 고정합니다. 이전 고정 커밋보다 2개 커밋 앞서며,
두 커밋 모두 패치셋 바깥의 `cpu_deep_idle` 코드
(`headers/private/kernel/cpu.h`, `src/system/kernel/cpu.cpp`)만 변경합니다.
이 커밋을 깨끗하게 체크아웃했을 때 19개 패치가 모두 적용됩니다.
다만 이 최신 고정 커밋으로 ISO 빌드와 부팅까지 다시 검증하지는 않았습니다.

이전에는 RenkuOS/Source의 커밋
[`4e5de412da344adf76bb2fa5bb68c8b970e961a3`](https://github.com/RenkuOS/Source/commit/4e5de412da344adf76bb2fa5bb68c8b970e961a3)
(2026-11-06)에 맞춰 패치를 다시 뽑았습니다. 이전 고정 커밋 이후 154개 커밋이 올라왔고, 그중
둘이 이 패치들 중 하나가 건드리는 파일을 같이 건드렸습니다. `0018-tcp-spawn-unbound`는
알고 보니 업스트림에 이미 들어가 있었습니다 -- `96d06c2e46`("tcp: _Spawn()
resets local address", 메인라인 Haiku에서 체리픽)가 똑같은 수정이라, 그
패치는 없앴습니다. `0005-arm64-pl031-rtc`는 없앨 게 아니라 다시 만들어야
했습니다: 관련 없는 커밋(`2fe0ded501`, ACPI용 캐시 가능 메모리 범위)이
`arch_kernel_args`에 자기 필드를 추가했는데, 하필 이 패치의 `pl031_rtc`
필드가 들어가던 바로 그 자리라 기존 diff의 맥락이 더는 맞지 않았습니다.
이제 그 필드는 바로 그 앞자리에 들어갑니다 -- 같은 구조체, 같은 의미는
그대로입니다. 그 고정 커밋을 깨끗하게 받아 남은 19개 패치를 모두
적용하고 `@minimum-anyboot`를 빌드했고, 그 ISO를 QEMU(`hvf`, `virtio-gpu-pci`,
`intel-hda`)에 유일한 USB 디스크로 붙여 데스크톱까지 부팅했습니다. PL031
시계가 실제 날짜를 읽고, `hda` 드라이버가 올라와 스트림이 설정되고,
`sshd`가 22번 포트에서 응답하고, Deskbar > Shut down으로 게스트가 꺼지면서
QEMU도 스스로 끝났습니다. 다른 트리에는 패치가 적용되지 않을 수 있습니다.
그때 `build-renku-arm64-iso.sh`는 반쯤 적용된 트리를 남기지 않고 그 사실을
알립니다.

그 전에 한 번 더, 커밋
[`f45bcca17e2279288a3a466ed4ed22667dd31ceb`](https://github.com/RenkuOS/Source/commit/f45bcca17e2279288a3a466ed4ed22667dd31ceb)
(2026-10-28)에 맞춰 다시 뽑았습니다. 이 커밋을 깨끗하게 받아 20개 패치를
모두(아직 중복이 아니었던 0018 포함) 적용하고 `@minimum-anyboot`를 빌드했습니다.

처음에는 커밋
[`47367b99ab8af5934adb4e709e9c0eaeb6485255`](https://github.com/RenkuOS/Source/commit/47367b99ab8af5934adb4e709e9c0eaeb6485255)
(2026-09-03)에서 뽑았고, 그 커밋으로 빌드해 Apple Silicon의 QEMU에서 부팅했습니다
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
