# Lab 04. 카메라 → 하드웨어 JPEG 인코딩 (+ TFT 스틸 프리뷰) — Synaptics Astra SR110 (sr100_rdk/sr100/m55)

## 이 실습의 목표

지금까지의 카메라 실습(Lab 01~03)은 RAW8 Bayer 원시 데이터를 애플리케이션 코드가 직접 다뤘습니다(통계 계산, 그레이스케일 변환). 이 실습은 SR110에 내장된 **JPEG 하드웨어 인코더(`syna,enc-video`)**를 이용해서, CPU 대신 전용 하드웨어가 카메라 프레임을 JPEG로 압축하는 것을 확인합니다. SDK가 제공하는 `drivers/video/enc` 샘플을 거의 그대로 사용합니다 — 이미 검증된 공식 샘플이라 별도 수정 없이도 동작합니다.

여기에 더해, 인코더가 만든 JPEG를 **다시 디코딩해서 Lab 03의 ST7789V3 TFT에 스틸(정지) 이미지로 표시**합니다. Lab 03는 RAW8 Bayer를 그레이스케일로만 보여줬지만, 이번에는 JPEG 디코딩이 색 정보를 온전히 복원해주므로 **실제 컬러 이미지**가 화면에 나타납니다.

> **하드웨어 검증 상태**: SR110 RDK 실기에서 검증 완료. JPEG 하드웨어 인코딩과, 이를 온보드에서 디코딩해 TFT에 컬러 스틸 이미지로 표시하는 것까지 모두 확인했습니다.

## 사전 준비

- 순수 Zephyr `video_api`만 사용합니다 — AI용 west 모듈은 필요 없습니다.
- 카메라 모듈이 SR110 RDK 보드에 이미 연결되어 있는 상태를 전제로 합니다(J23 커넥터, 별도 배선 불필요).
- **ST7789V3 TFT 모듈 + 레벨쉬프터가 필요합니다** — Lab 03와 동일한 배선입니다. Lab 03를 먼저 해보셨다면 배선을 그대로 재사용할 수 있습니다. (JPEG 인코딩 자체만 확인하고 싶다면 TFT 없이도 Step 1~2의 GDB 덤프 확인까지는 가능합니다 — "추가로 알면 좋은 내용" 참고.)
- 외부 USB-TTL 어댑터 — TFT(SPI0)를 켜면 콘솔이 UART0 대체 핀(GPIO44/45, J24 13/14번 핀)으로 이동하므로, 로그를 보려면 필요합니다(Lab 03와 동일한 제약).
- Lab 01/02를 먼저 해보셨다면 `video_api`(`video_set_format`/`video_enqueue`/`video_stream_start`/`video_dequeue`) 흐름이, Lab 03를 먼저 해보셨다면 TFT 배선과 회전+커버크롭 개념이 낯설지 않을 것입니다.

## 이 실습에서 알아보고자 하는 것

- SR110의 JPEG 하드웨어 인코더가 Lab 01~03처럼 애플리케이션이 RAW8 Bayer를 직접 가공하지 않고도, 카메라 프레임을 압축된 JPEG로 바로 만들어낼 수 있는지
- 이 인코더가 실제로는 `video_syna0`(Lab 01~03가 쓰던 범용 캡처 드라이버)를 거치지 않고, **센서(OV02C10)의 CSI 출력을 devicetree 레벨에서 인코더 노드에 직접 연결**하는 구조라는 점 — 즉 "카메라 → 캡처 드라이버 → 인코딩" 3단계가 아니라 "카메라 → 인코더" 2단계로 하드웨어 파이프라인이 짧아진다는 것
- 같은 인코더 노드가 devicetree `mode` 프로퍼티 하나로 완전히 다른 두 입력 경로(라이브 센서 / 예약메모리의 정적 패턴)를 선택할 수 있다는 것
- **JPEG처럼 압축된 포맷도, 임베디드 MCU 위에서 작은 소프트웨어 디코더 하나로 다시 압축 해제해서 화면에 띄울 수 있는지** — GDB 덤프 없이, 보드 스스로 "방금 압축한 걸 다시 풀어서 보여주는" 왕복(round-trip)이 가능한지

## 배워야 하는 것 (핵심 학습 목표)

- `syna,enc-video` 노드가 `video_syna0`(MIPI 캡처 드라이버)와 별개의 독립된 `video_api` 디바이스라는 점 — Lab 01~03와 똑같은 `video_set_format()`/`video_get_caps()`/`video_import_buffer()`/`video_enqueue()`/`video_stream_start()`/`video_dequeue()` API를 그대로 쓰지만, 입력 포맷이 아니라 **출력 포맷**(`VIDEO_PIX_FMT_JPEG`)을 설정한다는 차이
- devicetree `port`/`endpoint`로 두 장치(센서의 `ov02c10_out`, 인코더의 `lp_jpeg0_in`)를 `remote-endpoint-label`로 직접 연결하는 패턴
- JPEG 인코더 전용 예약 메모리(`reserved-memory`, `zephyr,memory-attr = <DT_MEM_ARM_MPU_RAM_NOCACHE>`)가 필요한 이유 — 인코더 하드웨어가 DMA로 직접 접근하는 non-cacheable 메모리 영역이 별도로 있어야 하고, 이 영역 안에서 "원본 프레임 저장용 오프셋(`frame-raw-offset`)"과 "JPEG 출력 최대 크기(`max-jpeg-size`)"가 나뉘어 있다는 것
- `mode = <0>`(라이브 센서 → 인코더 → 메모리)과 `mode = <1>`(예약메모리의 합성 컬러바 패턴 → 인코더 → 메모리)의 차이 — 후자는 카메라 없이도 인코더 자체 동작을 검증할 수 있는 하드웨어-독립적 테스트 경로
- 압축 결과물(JPEG)은 프레임마다 크기가 달라지므로, RAW 데이터처럼 고정 크기가 아니라 `video_buffer.bytesused`로 실제 인코딩된 바이트 수를 매번 확인해야 한다는 것
- **JPEG 디코딩을 아주 작은 임베디드 전용 라이브러리(TJpgDec)로 처리하는 방법** — libjpeg 같은 범용 라이브러리는 이런 소형 MCU의 플래시(ITCM) 예산에 맞지 않지만, TJpgDec은 코드 크기가 몇 KB 수준이고 MCU(최소 압축 단위) 블록 하나씩 콜백으로 돌려주는 방식이라 스트리밍 처리에 적합함
- **디코딩 시점에 축소(scale)까지 한 번에 처리하는 기법** — 원본 480x270을 먼저 다 풀고 나중에 축소하는 대신, TJpgDec의 `scale` 파라미터로 IDCT(역이산코사인변환) 단계에서 곧바로 1/2 크기(240x135)로 디코딩해서, 압축 해제 연산량과 프레임버퍼 메모리를 동시에 줄이는 방법
- Lab 03에서 만든 "회전 → 커버크롭" 좌표 변환이, 입력이 RAW8 Bayer든 디코딩된 RGB565든 상관없이 그대로 재사용 가능한 범용 기하 변환이라는 것 — 이번에는 2x2 Bayer 블록 평균(디베이어링)이 필요 없으므로 그 부분만 빠지고 좌표 변환 로직 자체는 동일함
- Lab 03는 매 프레임(초당 1회) 화면을 계속 갱신하는 "라이브 프리뷰"였지만, 이 실습은 **한 번 캡처한 정지 이미지 한 장만 화면에 표시하고 끝나는 "스틸 프리뷰"**라는 것 — 그래서 Lab 03처럼 갱신 간격을 조절하는 shell 명령이 필요 없음

## 추가로 알면 좋은 내용

- 이 샘플은 `CONFIG_VIDEO_SYNA_MIPI=n`으로 설정되어 있습니다 — Lab 01~03가 쓰던 범용 MIPI 캡처 드라이버(`video_syna0`)를 아예 켜지 않습니다. 카메라 센서(`ov02c10`)의 CSI 출력 endpoint가 devicetree에서 `video_syna0`가 아니라 인코더 노드(`lp_jpeg0`)를 가리키도록 오버레이가 다시 연결하기 때문입니다. 즉 Lab 01~03와 Lab 04는 같은 카메라 하드웨어를 쓰지만, **한 번에 하나의 파이프라인만 활성화**할 수 있습니다(별도 빌드 타깃이므로 문제없이 공존 가능).
- `mode = <1>`(예약메모리 입력)으로 빌드하면 카메라를 아예 연결하지 않고도 인코더 자체 동작을 확인할 수 있습니다 — 세로 컬러바(검정/파랑/시안/초록/마젠타/빨강/노랑/흰색) + 하단 25%에 체커보드 패턴을 합성 Bayer 프레임으로 만들어 인코더에 넣습니다. 이 모드는 소스가 960x540이라 이 실습의 TFT 표시 코드(480x270 → 240x135 디코딩 기준)와 해상도가 맞지 않으므로, TFT에는 표시되지 않고("크기가 다르다"는 경고 로그만 남기고 건너뜀) JPEG 인코더 자체 동작 확인 용도로만 씁니다.
- JPEG는 매 프레임 압축률이 장면에 따라 달라지므로 파일 크기가 고정되지 않습니다 — SDK 예시 로그를 보면 960x540(mode=1) 컬러바 패턴이 약 31KB로 압축되는데, 이 실습(480x270, mode=0) 실측 결과 이보다 작은 크기가 나왔습니다(정확한 수치는 실행 로그 참고).
- 예약 메모리 주소(`0xB4900000`)와 크기(`0x9D800`)는 SDK 샘플 값을 그대로 재사용했습니다 — `0x4000`(프로그래머 예약 영역) + `0x7E900`(960x540 RAW8 최대 크기, mode=1 기준) + `0x1AF00`(최대 JPEG 크기)로 구성되어 있어, mode=0(480x270)에서는 여유가 더 큽니다.
- **TJpgDec(Tiny JPEG Decompressor)**는 ChaN(FatFs 저자와 동일)이 만든 오픈소스 라이브러리로, `src/tjpgd.c`/`tjpgd.h`에 원본 그대로(라이선스: 사용 제한 없음, 저작권 표시만 유지) 가져다 놓았습니다. 설정 파일 `src/tjpgdcnf.h`만 이 랩에 맞게 조정했습니다 — 출력 포맷을 기본값(RGB888)이 아니라 ST7789V3가 그대로 받을 수 있는 **RGB565**로 바꿨습니다(`JD_FORMAT=1`).
- 디코딩 작업공간(`JD_WORK_SIZE`, 현재 4096바이트)이 부족하면 `jd_prepare()`/`jd_decomp()`가 `JDR_MEM1` 에러를 돌려줍니다 — 이 경우 `tft_still.c`의 `JD_WORK_SIZE`를 늘려야 합니다. ChaN의 문서에 따르면 보통 3.1KB 정도면 충분하다고 하지만, 실제 필요량은 이미지 구조(재시작 마커 유무 등)에 따라 달라질 수 있어 여유를 두었습니다.
- 화면 표시는 Lab 03의 "회전 90도 + 커버크롭"을 그대로 재사용하되, 입력이 RAW8 Bayer가 아니라 이미 디코딩된 RGB565 픽셀이라 2x2 블록 평균(디베이어링) 단계가 빠집니다 — 그 결과 Lab 03(그레이스케일)와 달리 이 실습은 **실제 색이 있는 이미지**가 화면에 표시됩니다.
- 카메라(I2C1)와 TFT(SPI0)는 Lab 03와 마찬가지로 물리적으로 완전히 분리된 핀이라 충돌이 없습니다. TFT를 켜는 순간 M55의 기본 콘솔(UART1, GPIO23/24 공유)을 잃는 것도 Lab 03와 동일한 제약입니다.
- 이 랩에서 TJpgDec을 새로 들여왔음에도 실기에서 플래시(ITCM) 초과 없이 정상적으로 빌드/동작함을 확인했습니다 — Lab 03가 shell 기능 추가로 겪었던 플래시 초과 문제([`03_tft_preview/doc/TROUBLESHOOTING_kr.md`](../../03_tft_preview/doc/TROUBLESHOOTING_kr.md) 참고)와 달리, 이 랩은 shell을 쓰지 않는 1회성 스틸 표시라 코드 크기가 더 작았던 것으로 보입니다. 그래서 이 랩은 별도 트러블슈팅 문서가 없습니다.

## 준비물

- Synaptics Astra SR110 RDK 보드 + OV02C10 카메라 모듈 (J23 커넥터로 이미 연결됨, 별도 배선 불필요)
- **ST7789V3 TFT 모듈 + 레벨쉬프터(TXS0108E 등)** — Lab 03와 동일한 배선(아래 표 참고).
- 외부 USB-TTL 어댑터 — TFT(SPI0)를 켜면 콘솔이 UART0 대체 핀(GPIO44/45, J24 13/14번 핀)으로 이동하므로, 로그를 보려면 필요합니다.
- USB 케이블, PC (WSL2 + west CLI 개발 환경) 또는 PowerShell(Windows 네이티브) 빌드/플래시 환경
- (선택, TFT 없이 JPEG 인코딩만 확인할 때) OpenOCD + `arm-none-eabi-gdb`, 일반 이미지 뷰어

### TFT 배선 (SPI0, 레벨 시프터 경유 — Lab 03와 동일)

| 신호 | 역할 | SR110 쪽 (1.8V) | 레벨 시프터 | 디스플레이 쪽 (3.3V) |
|---|---|---|---|---|
| VCC | 전원 | - | - | 3.3V |
| GND | 그라운드 | GND | GND (양쪽 공통) | GND |
| SCL/SCLK | SPI 클럭 | SPI0 CLK (SoC GPIO22, J25 11번 핀) | CH_A ↔ CH_B | SCL |
| SDA/MOSI | SPI 데이터 | SPI0 MOSI (SoC GPIO23, J25 14번 핀) | CH_A ↔ CH_B | SDA |
| CS | 칩 셀렉트 | SPI0 CS, 네이티브 하드웨어 CS (SoC GPIO21, J25 12번 핀) | CH_A ↔ CH_B | CS |
| RES/RST | 리셋 | SoC GPIO17, J24 3번 핀 | CH_A ↔ CH_B | RES |
| DC | Data/Command 선택 | SoC GPIO18, J24 4번 핀 | CH_A ↔ CH_B | DC |
| BLK | 백라이트 | - | - | 3.3V (직결, 레벨 시프터 안 거침) |

레벨 시프터의 저전압 쪽(VCCA)은 1.8V, 고전압 쪽(VCCB)은 3.3V에 연결합니다. TXS0108E를 쓴다면 **OE 핀은 1.8V(VCCA)**에 연결하세요. MISO는 배선하지 않습니다.

## 개념 — 카메라 → JPEG 인코더 → (새로 추가) 디코딩 → TFT

```
[mode=0, 라이브 센서 경로 — 이 실습의 기본값]

OV02C10 센서 CSI 출력 (ov02c10_out)
   ↓  devicetree endpoint로 직접 연결 (remote-endpoint-label)
JPEG 인코더 (lp_jpeg0, syna,enc-video) 입력
   ↓  video_set_format(VIDEO_PIX_FMT_JPEG, 480x270)로 출력 포맷 설정
   ↓  video_enqueue()로 출력 버퍼(JPEG 담을 곳) 2개 등록
   ↓  video_stream_start()로 인코딩 파이프라인 시작
   ↓  하드웨어가 센서 프레임을 받아 JPEG로 압축, 예약 메모리에 기록
video_dequeue() 로 완성된 JPEG 버퍼 회수 (bytesused = 실제 압축 크기)
   ↓
   ├─ (기존) GDB로 (버퍼 주소, 버퍼 주소+bytesused) 범위를 덤프 → .jpg 파일로 저장 → PC에서 확인
   └─ (신규) tft_still_show_jpeg(): 같은 JPEG 버퍼를 그대로 온보드에서 디코딩
        ↓  TJpgDec으로 1/2 축소 디코딩 (480x270 → 240x135 RGB565)
        ↓  Lab 03와 동일한 회전(90도) + 커버크롭 좌표 변환 (디베이어링 단계는 생략)
        ↓  한 줄(240픽셀=480바이트)씩 8바이트 청크로 SPI0 전송 — 패널 전체(240x280) 채움
        TFT 화면에 실제 컬러 스틸 이미지로 표시
```

Lab 01~03의 `video_dequeue()`는 RAW8 Bayer 원본을 그대로 돌려줬지만, 이 랩의 `video_dequeue()`는 **이미 압축이 끝난 JPEG 바이트열**을 돌려준다는 점이 가장 큰 차이입니다. 이번 실습에서는 그 JPEG 바이트열을 PC로 덤프하는 것에 더해, 보드 스스로 다시 풀어서 화면에 보여주는 왕복(encode → decode → display)까지 한 번에 확인합니다.

## Step 1. 빌드 및 플래시

`lab/`가 준비되어 있습니다. 기본값은 라이브 센서 모드(`mode=0`) + TFT 스틸 프리뷰입니다.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <워크스페이스>/SR110_Zephyr_camera/04_jpeg_encode/lab -d build_camera_jpeg_encode
```

(선택) 카메라 없이 인코더 자체만 확인하고 싶다면, 예약메모리 컬러바 패턴 모드(`mode=1`)로 빌드할 수도 있습니다(이 모드는 TFT에는 표시되지 않습니다 — 위 "추가로 알면 좋은 내용" 참고):

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <워크스페이스>/SR110_Zephyr_camera/04_jpeg_encode/lab -d build_camera_jpeg_encode_mem -- -DDTS_EXTRA_CPPFLAGS="-DENC_MEMORY_INPUT"
```

콤바인 이미지를 평소처럼 `openocd_flash.py`로 플래시합니다.

## Step 2. 실행 & 확인

콘솔(외부 USB-TTL 어댑터, UART0 대체 핀)에 아래와 비슷한 로그가 출력됩니다(실제 `bytesused` 값은 장면에 따라 달라집니다).

```
*** Booting Zephyr OS build v4.4.1 ***
[00:00:00.xxx,000] <inf> enc_video_sample: JPEG encoder validator start (live-to-memory, mode=0)
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_set_format ret=0 capacity=131072 pitch=0 (480x270)
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_get_caps ret=0 min_vbuf=2 align=64
[00:00:00.xxx,000] <inf> enc_video_sample: enc app_buf[0] addr=0x... size=131072 align=64
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_enqueue[0] ret=0
[00:00:00.xxx,000] <inf> enc_video_sample: enc app_buf[1] addr=0x... size=131072 align=64
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_enqueue[1] ret=0
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_stream_start ret=0
[00:00:00.xxx,000] <inf> enc_video_sample: enc video_dequeue ret=0
[00:00:00.xxx,000] <inf> enc_video_sample: Frame captured at addr=0x... bytesused=NNNNN
[00:00:00.xxx,000] <inf> enc_video_sample: Dump JPEG from 0x... (0x... + NNNNN)
tft_still: JPEG decoded (480x270) and shown on panel
[00:00:0X.xxx,000] <inf> enc_video_sample: JPEG encoder validator done ret=0
```

로그에 찍힌 `Frame captured at addr=... bytesused=...` 값은 기존과 동일하게 GDB 덤프에도 쓸 수 있고, `tft_still: JPEG decoded ... and shown on panel` 로그가 뜨면 화면 표시까지 성공한 것입니다.

### (선택) JPEG 덤프하기 (GDB)

TFT 없이 PC에서도 같은 이미지를 확인하고 싶다면 Lab 01과 동일한 방식으로 덤프할 수 있습니다.

**터미널 1 (OpenOCD)**
```bash
openocd -f boards/syna/astra_sr/sr100/support/openocd.cfg
```

**터미널 2 (GDB)**
```
arm-none-eabi-gdb
target extended-remote :3333
dump binary memory out.jpg <버퍼_주소> (<버퍼_주소> + <bytesused>)
```

예를 들어 로그가 `Frame captured at addr=0x33ea64c0 bytesused=28450`이라면:
```
dump binary memory out.jpg 0x33ea64c0 (0x33ea64c0 + 28450)
```

확인할 포인트:

- 빌드/플래시 후 에러 로그 없이 `video_set_format`/`video_get_caps`/`video_enqueue`/`video_stream_start`/`video_dequeue` 모두 `ret=0`으로 성공하는지
- `bytesused`가 0이 아니고, 합리적인 크기(수 KB~수십 KB)로 찍히는지
- `tft_still: JPEG decoded (480x270) and shown on panel` 로그가 뜨고, 실제 TFT 화면에 카메라가 보고 있는 장면이 **컬러**로 표시되는지
- 표시된 이미지가 Lab 03와 마찬가지로 90도 회전되어 있고, 패널 전체(240x280)를 여백 없이 채우는지
- (선택) GDB로 덤프한 `out.jpg`를 PC 이미지 뷰어로 열어서, TFT에 표시된 것과 같은 장면인지 비교
- (선택, `mode=1`로 빌드한 경우) 세로 컬러바 + 하단 체커보드 패턴이 JPEG로 정상 인코딩되는지 — TFT에는 표시되지 않고 "크기가 다르다"는 경고 로그만 남는 것이 정상

## 확인용 체크리스트

- [x] `west build -b sr100_rdk/sr100/m55` 빌드 성공 (mode=0, 라이브 센서)
- [x] 콘솔 로그에서 `video_set_format`/`video_get_caps`/`video_enqueue`/`video_stream_start`/`video_dequeue`가 모두 에러 없이 성공
- [x] `bytesused`가 0이 아닌 합리적인 크기로 찍힘
- [x] `tft_still: JPEG decoded ... and shown on panel` 로그가 뜨고, TFT에 실제 카메라 장면이 컬러 스틸 이미지로 표시됨
- [x] 표시된 이미지가 90도 회전 + 패널 전체를 채우는 형태로 보임 (Lab 03와 동일한 기하 변환)
- [ ] (선택) `mode=1`(예약메모리 컬러바 패턴)으로도 빌드해서 카메라 없이 인코더 동작 확인

## 다음

Lab 05(USB CDC로 PC 실시간 스트리밍)로 이어집니다.
