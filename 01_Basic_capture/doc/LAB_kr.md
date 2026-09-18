# Lab 01. MIPI 카메라 프레임 캡처 기초 — Synaptics Astra SR110 (sr100_rdk/sr100/m55)

## 이 실습의 목표

SR110 온보드 카메라 커넥터(J23)에 연결된 **OV02C10 센서(MIPI CSI-2, 1-lane)**로부터 Zephyr의 표준 `video_api`를 이용해 프레임을 캡처하고, 캡처 성공 여부·크기·타임스탬프를 콘솔 로그로 확인합니다. NPU/AI 없이 "카메라에서 이미지 한 장을 받아온다"는 가장 기본적인 단계만 다루는, 이 카메라 실습 시리즈의 "Hello World"입니다.

> **하드웨어 검증 완료**: SR110 RDK 실기에서 빌드/플래시/캡처 10프레임 모두 정상 동작을 확인했습니다 (아래 "실행 결과" 참고).

## 사전 준비

- 이 실습은 TFLM 등 AI용 west 모듈이 필요 없습니다 — 카메라 캡처는 순수 Zephyr `video_api` 기능이라 별도 모듈 없이 바로 진행할 수 있습니다.
- 카메라 모듈은 SR110 RDK 보드에 이미 연결되어 있는 상태를 전제로 합니다(J23 커넥터). 아직 연결 안 되어 있다면 먼저 연결해주세요.

## 이 실습에서 알아보고자 하는 것

- Zephyr `video_api`(`video_get_caps` → `video_set_format` → 버퍼 enqueue → `video_stream_start` → `video_dequeue` 반복 → `video_stream_stop`)의 표준 흐름이 SR110의 MIPI CSI-2 카메라에서 실제로 동작하는지
- 카메라 센서가 실제로 프레임을 몇 ms 만에 한 장씩 넘겨주는지, 캡처된 원시 데이터가 기대한 크기(해상도×포맷)와 일치하는지

## 배워야 하는 것 (핵심 학습 목표)

- Zephyr `video_api`의 표준 캡처 패턴 (buffer 준비 → enqueue → stream start → dequeue → 재사용/해제)
- MIPI CSI-2 카메라가 SR110에서 두 개의 서로 다른 버스로 나뉘어 연결되는 구조: **제어(SCCB, I2C1)**와 **영상 데이터(CSI-2 레인)**가 물리적으로 분리되어 있다는 점
- RAW8 Bayer(`VIDEO_PIX_FMT_SRGGB8`) 포맷이 무엇이고, 왜 카메라가 곧바로 RGB가 아니라 이 포맷으로 데이터를 내보내는지
- devicetree에서 카메라 드라이버(`video_syna0`, `syna,mipi-video`)와 센서(`ov02c10`)가 어떻게 표현되고, `status = "okay"`가 기본 활성화를 의미한다는 것

## 추가로 알면 좋은 내용

- 이 샘플은 해상도를 Kconfig `choice`(WQVGA/FHD)로 선택하도록 설계되어 있습니다. FHD(1920x1080)는 프레임 하나가 2MB에 달해 예약 메모리(SHM) 데비이스트리 오버레이가 별도로 필요합니다 — 이 랩은 오버레이가 필요 없는 WQVGA(480x270)만 다룹니다. FHD를 시도해보고 싶다면 원본 SDK 샘플(`zephyr_srsdk/samples/drivers/video/mipi_capture`)의 `boards/sr100_rdk_m55_fhd.overlay`를 참고하세요.
- 캡처된 원시 프레임은 콘솔에 찍히는 게 아니라 보드 메모리 안에만 존재합니다. 실제 이미지로 눈으로 보려면 OpenOCD+GDB로 메모리를 덤프해서 PC로 가져온 뒤 `ffplay`로 디베이어링(Bayer→RGB 변환)해서 봐야 합니다 — 아래 "Step 3" 참고.
- `video_dequeue()`가 기존에 enqueue한 버퍼를 그대로 돌려주는 구조이므로, 마지막 프레임만 별도로 보존(`retain_capture_buffer`)하고 나머지는 즉시 재-enqueue하는 것이 이 샘플의 버퍼 재사용 패턴입니다 — 카메라처럼 버퍼 풀이 제한적인 환경에서 흔한 설계입니다.
- 센서 전원/리셋은 GPIO 익스팬더(`pca6416@20`)를 통해 제어됩니다 (`powerdown-gpios`, `shutdown-gpios`) — 카메라가 응답하지 않을 때 흔한 원인 중 하나가 바로 이 GPIO 익스팬더 관련 초기화 문제입니다.

## 준비물

- Synaptics Astra SR110 RDK 보드 + OV02C10 카메라 모듈 (J23 커넥터로 이미 연결됨, 별도 배선 불필요)
- USB 케이블, PC (WSL2 + west CLI 개발 환경)
- 시리얼 터미널 (PuTTY / TeraTerm / screen / minicom)
- (선택) 프레임을 실제 이미지로 확인하고 싶다면: OpenOCD, `arm-none-eabi-gdb`, `ffplay`(ffmpeg 패키지에 포함)

## 개념 — SR110의 카메라 데이터 경로

카메라 한 대가 SR110과 맺는 연결은 사실 두 개의 독립된 버스입니다.

```
[OV02C10 센서]
   ├─ 제어(SCCB, 2-wire) ──→ SR110 I2C1 (ov02c10@36, 레지스터 설정/전원 제어)
   └─ 영상 데이터(MIPI CSI-2, 1-lane) ──→ SR110 CSI-2 입력 ──→ video_syna0 드라이버(syna,mipi-video)
```

- `ov02c10` devicetree 노드는 I2C1 위에 있고, 센서의 초기화 레지스터 값·해상도 설정·전원(GPIO 익스팬더 경유)·MCLK를 제어합니다.
- `video_syna0` devicetree 노드(`compatible = "syna,mipi-video"`)는 실제 CSI-2 레인에서 들어오는 영상 데이터를 받아 지정한 메모리 버퍼로 DMA하는 캡처 엔진입니다.
- 애플리케이션은 이 `video_syna0`만 Zephyr 표준 `video_api`로 다루면 됩니다 — I2C1 제어는 드라이버 내부에서 알아서 처리하므로 애플리케이션 코드에서 신경 쓸 필요가 없습니다.
- 센서가 내보내는 원시 데이터는 RGB가 아니라 **RAW8 Bayer 패턴**(`VIDEO_PIX_FMT_SRGGB8`)입니다 — 픽셀마다 R/G/B 중 하나의 값만 있고, 실제 컬러 이미지로 보려면 PC 쪽에서 디베이어링(interpolation)을 거쳐야 합니다. 이 랩에서는 이 변환을 굳이 보드에서 하지 않고, 필요하면 `ffplay`가 대신 해주도록 합니다.

## Step 1. 빌드 및 플래시

`lab/`이 준비되어 있습니다. WQVGA 해상도가 Kconfig 기본값이라 추가 옵션 없이 바로 빌드할 수 있습니다.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <워크스페이스>/SR110_Zephyr_camera/01_basic_capture/lab -d build_camera_capture
```

콤바인 이미지를 평소처럼 `openocd_flash.py`로 플래시합니다.

## Step 2. 실행 & 확인 (SR110 RDK 실측)

플래시 후 시리얼 콘솔에서 실제로 확인된 로그입니다 (주소/시간 값은 매 실행마다 달라질 수 있습니다).

```
*** Booting Zephyr OS build v4.4.1 ***
[00:00:00.357,000] <inf> video_sample_app: [TS] MIPI_CAPTURE_MAIN_ENTER uptime_ms=357
[00:00:00.361,000] <inf> video_sample_app: Zephyr RAM window: [0x33eb9460..0x3416f000)
[00:00:00.366,000] <inf> video_sample_app: Sample frame dump buffer: 129600 bytes at 0x33ecb4bc
[00:00:00.371,000] <inf> video_sample_app: Video caps: min_vbuf_count=2 align=64
[00:00:00.453,000] <inf> video_sample_app: Video format configured: 480x270 pitch=480 size=129600
[00:00:00.460,000] <inf> video_syna_sr100_mipi: Using internal SHM pool: addr=0x33eeaf40 size=524288
[00:00:00.465,000] <inf> video_syna_sr100_mipi: Sensor input bpp=10, driver output pixelformat=0x42474752
[00:00:00.470,000] <inf> video_syna_sr100_mipi: Configuring csi=0 shm=0x33eeaf40 size=129600
[00:00:00.476,000] <inf> video_syna_sr100_mipi: Stream started (480x270 size=129600)
[00:00:00.496,000] <inf> video_sample_app: Video stream started in 139 ms
[00:00:00.500,000] <inf> video_sample_app: Waiting for frame 1/10...
[00:00:00.525,000] <inf> video_sample_app: Frame 1 captured: bytesused=129600 timestamp=525 buffer=0x33f6afc0
[00:00:00.530,000] <inf> video_sample_app: Frame 1 captured and available in RAM in 26 ms
[00:00:00.540,000] <inf> video_sample_app: Stored captured frame: 129600 bytes at 0x33ecb4bc
...
[00:00:00.810,000] <inf> video_sample_app: Waiting for frame 10/10...
[00:00:00.823,000] <inf> video_sample_app: Frame 10 captured: bytesused=129600 timestamp=823 buffer=0x33f8aa40
[00:00:00.833,000] <inf> video_sample_app: Stored captured frame in-place: 129600 bytes at 0x33f8aa40
[00:00:00.843,000] <inf> video_syna_sr100_mipi: Stream stopped
[00:00:00.846,000] <inf> video_sample_app: Video sample finished
[00:00:00.849,000] <inf> video_sample_app: Sample completed in 492 ms from start
```

확인된 포인트:

- `Video format configured: 480x270 pitch=480 size=129600` — 요청한 해상도/포맷대로 설정됨
- `Frame N captured: bytesused=...` 로그가 10번(기본 `VIDEO_SAMPLE_CAPTURE_COUNT=10`) 모두 반복 출력, 매번 `bytesused=129600`으로 일정 — 프레임 드롭이나 크기 불일치 없음
- 첫 프레임부터 마지막 프레임까지 전체 스트림이 시작~종료까지 약 492ms 만에 끝남 (스트림 시작 자체는 139ms 소요, 이후 10프레임 캡처는 훨씬 빠르게 진행)
- 에러 없이 `Stream stopped` → `Video sample finished`로 정상 종료
- `driver output pixelformat=0x42474752`은 ASCII로 `"BGRG"`(리틀엔디언 FourCC라 실제로는 `"GRGB"`/Bayer 배열 순서 표기) — 드라이버가 내부적으로 RAW8 Bayer 포맷을 유지하고 있음을 확인
- 참고로 `video_ov02c10` 쪽 초기화 로그(pinctrl/전원 GPIO/MCLK 등)는 이번 실행에서 `<dbg>` 레벨이라 `CONFIG_LOG_DEFAULT_LEVEL=3`(info)에서는 보이지 않았습니다 — 정상이며, 센서가 실제로 응답해서 프레임이 들어오고 있으므로 초기화 자체는 이상 없습니다.

## Step 3. 프레임을 이미지로 보기 (선택, 하드웨어 검증 완료)

마지막(10번째) 프레임은 `store_captured_frame()`이 보존해두므로, 로그에 찍힌 `Stored captured frame: 129600 bytes at 0x...` 주소를 그대로 이용해 덤프할 수 있습니다.

**PowerShell(Windows 네이티브)에서 진행하는 경우** — 이번에 실제로 검증된 방법입니다. `openocd_flash.py`로 플래시할 때 쓰시는 것과 같은 `openocd.exe`/`.cfg`를 그대로 씁니다.

```powershell
# 창 1: OpenOCD를 서버 모드로 실행 (플래시용 wrapper가 아니라 openocd.exe를 직접 cfg만 지정해서 실행)
& "<openocd.exe 경로>" -f "<...>\syna_zephyr\srsdk_tools\Input_Config\sr100_m55.cfg"
# "Listening on port 3333 for gdb connections"가 뜨면 이 창은 그대로 켜둡니다.
```

Windows용 Arm GNU 툴체인(`arm-none-eabi-gdb.exe`, [Arm 공식 다운로드](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads))을 설치한 뒤, 새 PowerShell 창에서:

```powershell
$env:Path += ";C:\arm-gnu-toolchain\bin"   # 설치 경로에 맞게 수정
arm-none-eabi-gdb
target extended-remote :3333
dump binary memory frame_dump.raw <로그에 찍힌 주소> (<로그에 찍힌 주소> + 129600)
```

Windows용 ffmpeg(ffplay 포함, [ffmpeg.org](https://ffmpeg.org/download.html#build-windows))로 RAW8 Bayer 덤프를 디베이어링해서 미리봅니다.

```powershell
ffplay -hide_banner -loglevel error -f rawvideo -pixel_format bayer_rggb8 -video_size 480x270 frame_dump.raw
```

**WSL(Linux)에서 진행하는 경우** — OpenOCD를 WSL 안에서 실행할 때(USB 프로브를 `usbipd`로 넘긴 경우)의 방법입니다.

```bash
# 터미널 1: OpenOCD 실행 (zephyr_srsdk 리포지토리 루트에서)
openocd -f boards/syna/astra_sr/sr100/support/openocd.cfg

# 터미널 2: GDB로 접속 후 덤프 (gdb-multiarch 또는 arm-none-eabi-gdb)
gdb-multiarch
target extended-remote :3333
dump binary memory frame_dump.raw <로그에 찍힌 주소> (<로그에 찍힌 주소> + 129600)
```

```bash
# RAW8 Bayer 덤프를 디베이어링해서 미리보기
ffplay -hide_banner -loglevel error -f rawvideo -pixel_format bayer_rggb8 -video_size 480x270 frame_dump.raw
```

> **참고**: OpenOCD와 GDB는 반드시 같은 환경(둘 다 PowerShell 또는 둘 다 WSL)에서 실행해야 합니다 — `localhost:3333` 연결이 WSL2 네트워크 설정에 따라 다른 환경끼리는 안 될 수 있습니다.

## 확인용 체크리스트

- [x] `west build -b sr100_rdk/sr100/m55` 빌드 성공
- [x] 플래시 후 콘솔에서 `Video format configured: 480x270 ...` 로그 확인
- [x] `Frame N captured` 로그가 10회 반복 출력, `bytesused=129600` 고정
- [x] `Video sample finished`로 정상 종료
- [x] GDB 덤프 + `ffplay`로 실제 촬영된 이미지 확인 (PowerShell 환경에서 검증 완료)

## 다음

Lab 02(연속 캡처 + 밝기 통계), Lab 03(카메라 → TFT 실시간 프리뷰), Lab 04(JPEG 하드웨어 인코딩), Lab 05(USB CDC로 PC 실시간 스트리밍)로 이어집니다.
