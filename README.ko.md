# RENKU arm64

[English](README.md)

[RenkuOS](https://github.com/RenkuOS/Source)를 `arm64`용으로 빌드한 것입니다.
Apple Silicon에서 x86 이미지를 에뮬레이션하는 대신 Hypervisor.framework로
네이티브 속도로 돕니다.

## 순정 RenkuOS/Source와 다른 점

RenkuOS/Source는 그 자체로 이미 arm64에서 빌드되고 부팅됩니다 -- 여기서
arm64 포팅 자체를 패치하는 건 없습니다. 이 디렉터리가 더하는 건
`@minimum-mmc`/`@minimum-anyboot` 빌드 프로파일이 (아키텍처 상관없이) 원래
빼놓는 것들, 그리고 arm64에서만 터지는 몇 가지 수정입니다:

| | 순정 RenkuOS/Source (arm64) | `arm64-patch/`(이 저장소) 적용 |
|---|---|---|
| 오디오 | 전혀 없음 -- `/dev/audio`도, `media_server`도 없음 | 녹음·재생 전부 가능 |
| 브라우저 | 빌드 안 됨 | WebPositive |
| 커맨드라인 | `curl`, `wget`, `tar`, `gzip` 없음 | 전부 포함 |
| 시스템 시계 | 매 부팅마다 1970년에서 시작(TLS 깨짐) | 실시간 시계를 읽음 |
| 설치 가능한 매체 | `@minimum-anyboot`가 arm64에서 아예 빌드 안 됨 | 빌드되고, 부팅되고, 디스크에 설치까지 됨 |
| 한글/일본어/중국어 | 빈 네모로 나옴 | 번들된 폰트로 표시됨 |
| R Chromium | 필요한 커널 수정이 없음 | 포함됨 |
| SSH | HaikuPorts arm64에 아예 없음 | `sshd`가 부팅 시 실행됨 |
| 앱 설치 도구 | 없음 | R AppMarket이 이미지에 들어 있음 |
| 패키지 저장소 | 루트 인증서 없음, 설정된 HaikuPorts 저장소에 arm64 패키지가 없음 | 인증서 포함, RENKU Apps 저장소가 미리 설정됨 |
| HaikuPorts 패키지 | packagefs가 거의 모든 패키지가 쓰는 zstd 압축을 못 읽음 | 정상적으로 읽고 설치됨 |
| 커널 안정성 | TCP 연결이 타이머 도중 정리되거나 네트워크가 내려갈 때 패닉 | 고쳐짐 |

20개 패치 각각이 정확히 무엇을 바꾸고 왜 그런지는
[`AGENTS.md`](AGENTS.md)와 [`arm64-patch/README.md`](arm64-patch/README.ko.md)에
있습니다.

## 요구 사항

- Mac (네이티브 속도를 원하면 Apple Silicon, Intel도 에뮬레이션으로 가능)
- Docker Desktop, Settings > General > "Use Virtualization Framework" 켜기
- `brew install qemu`

## 현재 ISO

RenkuOS/Source 커밋 `8484655099aae479b59590d098dfcbf6fdc4d908`에서 빌드한
`renku-arm64.iso`의 SHA-256은
`a17e71567b58539af254746c6c3fad4df79331d9cb0ff183bfc50de114c06168`입니다.
[GitHub 릴리스에서 다운로드](https://github.com/rainygirl/renkuos-arm64/releases/tag/2026.11.06)할 수 있습니다.

## ISO 빌드

```sh
./build-renku-arm64-iso.sh              # -> ./renku-arm64.iso
```

첫 실행은 몇 시간 걸리고 대부분이 크로스 툴체인입니다. 한 번 만들어지면:

```sh
SKIP_CROSS_TOOLS=1 ./build-renku-arm64-iso.sh
```

로 재사용해서 훨씬 빨리 끝납니다.

## QEMU로 띄워보기

`./run-qemu-renku-arm64.sh`를 그냥 실행하기 전에 이것부터 하세요: 막 받은
상태엔 아직 아무것도 설치돼 있지 않아서, `--install` 없이 먼저 부팅하면
OS가 하나도 없는 빈 디스크만 만들어집니다.

```sh
./run-qemu-renku-arm64.sh --install
```

(빈) 디스크를 붙인 채로 ISO를 부팅합니다 -- 디스크는 처음 실행할 때 자동으로
만들어집니다. 그 다음 게스트 안에서:

1. **DriveSetup**: 타겟 디스크 선택, `Disk > Initialize > GUID Partition
   Map`. 약 64MiB짜리 파티션을 *EFI system data* 타입으로 만들고 FAT32로
   포맷. 나머지 전체를 *Be File System* 타입으로 두 번째 파티션 만들고
   포맷.
2. **Installer**: `Haiku`에서 방금 만든 Be File System 파티션으로 설치.
3. **Terminal**에서, EFI 펌웨어가 찾는 자리에 부트로더를 넣습니다
   (Installer가 EFI에서는 이 단계를 자동으로 안 해줍니다):
   ```sh
   mountvolume -all
   cp -r "/haiku esp/EFI" "/efi/"
   sync
   ```
4. QEMU 창을 닫거나 다른 방식으로 끄기 전에 **게스트 안에서 정상적으로
   종료**하세요 -- Deskbar 메뉴 > Shutdown > Power off. 이게 중요합니다:
   전원이 끊기기 전에 쓰기 캐시가 디스크에 반영되지 않으면, 설치된 Haiku
   디스크가 *다음* 부팅에서 부트로더나 시스템 패키지가 깨진 채로 뜰 수
   있습니다. 정상 종료하면 이 문제가 아예 안 생기고, 이 포팅은 정상
   종료 시 QEMU도 알아서 꺼집니다.

그 다음부터는 `./run-qemu-renku-arm64.sh`(`--install` 없이)로 설치된 디스크를
바로 부팅합니다. `--headless`는 창 없이 시리얼 콘솔만 씁니다. 디스크
(`renku-arm64-vm.img`)는 어느 쪽이든 계속 남습니다.

기본으로 전체화면으로 시작합니다(Cmd-F로 전환, `--window`는 창으로 시작). 전체화면은 맥북
카메라 노치 양옆까지 화면 전체를 씁니다. macOS는 QEMU 자체 전체화면을 노치
아래에 두기 때문에, 스크립트가 `tools/qemu-notch/libqemu-notch.dylib`(첫
실행 때 빌드)를 주입합니다. QEMU는 그대로 macOS 기본 전체화면(자기 Space,
Cmd-Tab과 스와이프로 다른 앱 전환 가능)에 들어가고, dylib가 그 Space 안에서
화면 전체를 덮는 창으로 QEMU 화면을 옮깁니다. 게스트에는 디스플레이 자체
해상도(13인치 맥북 에어는 2560x1664)와 실제 크기(EDID)를 알려서, R Retina
(UI 배율 자동)가 스스로 2배를 고릅니다. `RENKU_RES=WxH`로 해상도 지정,
`RENKU_GUEST_SCALE=2`면 두 배, `RENKU_NOTCH=0`이면 dylib를 쓰지 않습니다.

## SSH

`sshd`가 부팅 시 실행됩니다. 스크립트가 이미 호스트 포트를 연결해뒀습니다:

```sh
ssh -p 2222 baron@127.0.0.1
```

`~/config/settings/ssh/authorized_keys`(Haiku의 `~/.ssh`에 해당)에 키를
넣어두면 됩니다. 비밀번호 로그인은 안 됩니다 -- 이유는
[`arm64-patch/README.md`](arm64-patch/README.ko.md) 참고.

## 알려진 한계

WebPositive에서 HTML5 오디오·비디오가 재생되지 않습니다(유튜브가 재생 못
한다고 표시). 이건 HaikuWebKit 자체의 결함이고 여기서 고친 건 아닙니다 --
자세한 내용은 [`AGENTS.md`](AGENTS.md) 참고.

## 라이선스

이 저장소 자체의 작업물(스크립트, 패치, 문서)은 MIT입니다 --
[`LICENSE`](LICENSE) 참고. RenkuOS 자체는 이 라이선스가 아니라 자기 자신의
라이선스를 따릅니다.
