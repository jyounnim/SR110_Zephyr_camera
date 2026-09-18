# Lab 05. USB CDC로 PC에 실시간 스트리밍 — Synaptics Astra SR110 (sr100_rdk/sr100/m55)

## 이 실습의 목표

Lab 04까지는 카메라 프레임을 보드 위에서만(GDB 덤프, 또는 TFT 스틸 이미지) 확인할 수 있었습니다. 이번 실습에서는 **JPEG 하드웨어 인코더가 만든 프레임을 USB를 통해 PC로 실시간으로 계속 흘려보내고**, PC에서 파이썬 스크립트로 그 영상을 실시간 동영상처럼 화면에 띄웁니다. 보드에 물리 디스플레이를 연결하지 않고도 "지금 카메라가 보고 있는 장면"을 실시간으로 확인할 수 있는 방법입니다.

> **하드웨어 검증 완료**: SR110 RDK 실기에서 빌드/플래시/USB CDC 열거/실시간 스트리밍까지 모두 확인했습니다. 개발 과정에서 겪은 플래시 용량 초과, USB 컨트롤러 초기화 실패 두 가지 문제와 해결 방법은 [`TROUBLESHOOTING_kr.md`](./TROUBLESHOOTING_kr.md)에 정리했습니다.

## 이 실습에서 알아보고자 하는 것

- SR110은 USB CDC ACM(가상 COM 포트)으로 열거되어, 별도의 드라이버 설치 없이 PC에서 일반 시리얼 포트처럼 열 수 있음을 확인합니다.
- 보드가 매 프레임 JPEG를 인코딩해서 USB로 보내고, PC가 그걸 받아서 디코딩+화면 표시하는 파이프라인 전체(캡처 → 인코딩 → USB 전송 → 수신 → 디코딩 → 표시)를 끝까지 연결해봅니다.
- 호스트가 준비되기 전에는 보드가 스트리밍을 시작하지 않도록 하는 "DTR(가상 시리얼 연결 신호) 대기" 패턴을 살펴봅니다.

## 배워야 하는 것

- **USB CDC ACM**: USB로 가상 시리얼 포트를 흉내내는 표준 USB 클래스. 호스트 OS는 별도 드라이버 없이 `/dev/ttyACM0`(Linux/WSL) 또는 `COMx`(Windows)로 인식합니다.
- **Zephyr의 `sample_usbd` 헬퍼**: USB 디바이스 스택(USBD) 초기화, VID/PID 설정 등 USB 부트스트랩 보일러플레이트를 감싸주는 Zephyr 표준 샘플 유틸리티(`samples/subsys/usb/common/`) — 이 랩과 SDK의 `mipi_capture_to_enc` 샘플이 공통으로 재사용합니다.
- **DTR(Data Terminal Ready) 대기**: 호스트가 가상 COM 포트를 열면(파이썬에서 `serial.Serial(...)`로 포트를 열면) DTR 신호가 세팅됩니다. 보드는 이 신호가 뜰 때까지 스트리밍을 시작하지 않고 대기해서, "호스트 뷰어가 아직 안 열렸는데 데이터를 마구 보내다 버려지는" 상황을 피합니다.
- **프레이밍(길이/체크섬 헤더)**: 시리얼은 스트림(경계 없는 바이트 흐름)이라, 매 프레임 앞에 "이 프레임이 몇 바이트인지 + CRC32 값"을 담은 16바이트 헤더를 붙여야 수신 측에서 프레임 경계를 정확히 찾고 손상 여부를 확인할 수 있습니다.
- **인터럽트 기반 UART 송신**: `uart_irq_tx_enable()`/`uart_fifo_fill()`를 이용해 CPU가 바쁘게 폴링하지 않고 인터럽트로 송신 완료를 통지받는 방식(busy-wait 대신 세마포어로 대기).

## 추가로 알면 좋은 내용

- **이 랩이 SDK 원본(`mipi_capture_to_enc`)과 다른 점**: SDK 원본 샘플은 FHD(1920x1080) 한 프레임을 캡처해서 960x540 사분면(quadrant) 4장으로 나눠 인코딩+전송하고, XSPI 플래시 저장이나 UVC(USB Video Class) 전송 같은 옵션 기능까지 포함한 훨씬 복잡한 캡스톤급 샘플입니다. 이 랩은 지금까지(Lab 01~04) 이 커리큘럼이 계속 써온 **단일 480x270 프레임 구조를 그대로 유지**하고, 사분면 분할·XSPI 저장·UVC는 넣지 않았습니다 — 대신 그 480x270 프레임을 **끊임없이 반복해서(무한 루프)** 전송하는 방식으로 "실시간 스트리밍"을 구현했습니다. USB 전송 방식도 CDC ACM 하나만 씁니다(WSL+`usbipd` 워크플로와도 맞물리는 경로입니다).
- **USB CDC 전송 코드(`usb_cdc_transport.c`/`.h`)는 원본에서 한 글자도 안 바꾸고 그대로 가져왔습니다**: 이 파일은 "헤더+임의 크기 JPEG 버퍼 하나를 보낸다"는 범용 함수라서 애초에 FHD나 사분면에 얽매인 코드가 없었기 때문입니다.
- **`main.c`는 Lab 04/원본 `enc` 샘플의 단발성 캡처 흐름을 반복 루프로 바꾼 버전**입니다: 인코더 설정 → 버퍼 큐 → 스트림 시작까지는 동일하고, 그 뒤 "디큐 → USB로 전송 → 같은 버퍼 재인큐"를 무한 반복합니다. 호스트가 뷰어를 껐다(포트를 닫았다) 다시 켤 수 있도록, 전송 실패 시 스트림을 정리하고 재부팅해서 다시 DTR을 기다리는 구조로 만들었습니다.
- **mode=1(예약메모리 컬러바)도 선택 실습으로 지원**하지만, 이 모드는 카메라가 없으니 매번 똑같은 정지 이미지를 인코딩합니다 — 무한 스트리밍은 의미가 없어서 30프레임만 반복 전송하고 멈추도록 했습니다(카메라 없이도 USB CDC 파이프라인 자체가 동작하는지 확인하는 용도).
- **호스트 파이썬 스크립트(`tools/recv_stream_cdc.py`)**: SDK 원본의 `recv_quadrants_cdc.py`(4장 받고 종료)를 "무한 루프로 계속 받아서 OpenCV 창에 계속 갱신"하는 형태로 고쳤습니다. 헤더 포맷(`QBUS` 매직 + 버전 + 프레임ID + 길이 + CRC32)은 원본과 동일해서 파싱 로직은 그대로 재사용했습니다. `q` 키를 누르면 종료됩니다.
- **필요 패키지**: `pip install pyserial opencv-python numpy` (PC 쪽, WSL이든 Windows 네이티브 파이썬이든 상관없음).
- **빌드 시 `-DCONFIG_XIP=n` 옵션이 필요합니다** — 자세한 이유는 [`TROUBLESHOOTING_kr.md`](./TROUBLESHOOTING_kr.md)를 참고하세요.

## 준비물

- SR110 RDK 보드 (USB 케이블로 PC와 연결 — 카메라 랩에서 쓰던 디버거용 USB와는 별개로, 보드의 USB 디바이스 포트가 필요합니다)
- PC (Windows 또는 WSL) — 시리얼 포트를 열 수 있는 환경, Python 3 + `pyserial`/`opencv-python`/`numpy`

## 개념 다이어그램

```
[카메라(ov02c10)] --CSI--> [JPEG 인코더(lp_jpeg0)] --dequeue--> [main.c 루프]
                                                                    |
                                                          cdc_transport_send_jpeg()
                                                                    |
                                                                    v
                                                        [USB CDC ACM 가상 COM 포트]
                                                                    |
                                                                    v (USB 케이블)
                                                        [PC: recv_stream_cdc.py]
                                                          헤더 파싱 -> JPEG 디코드
                                                          -> OpenCV 창에 실시간 표시
```

## Step 1. 빌드 & 플래시

`lab/`로 준비되어 있습니다. 기본값은 라이브 센서 모드(`mode=0`, 480x270)로 무한 스트리밍합니다.

플래시 이미지가 기본 링커 설정으로는 flash(FLASH) 영역을 초과하므로, **`-DCONFIG_XIP=n`**(코드를 flash에서 직접 실행하는 XIP 대신 RAM에서 실행)을 반드시 함께 지정해야 합니다 — 원인과 대안은 [`TROUBLESHOOTING_kr.md`](./TROUBLESHOOTING_kr.md) 1번 항목 참고.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <워크스페이스>/SR110_Zephyr_camera/05_usb_stream/lab -d build_camera_usb_stream -- -DEXTRA_CONF_FILE=prj_cdc.conf -DCONFIG_XIP=n
```

(선택) 카메라 없이 USB CDC 파이프라인만 확인하고 싶다면 예약메모리 컬러바 모드(`mode=1`)로 빌드:

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <워크스페이스>/SR110_Zephyr_camera/05_usb_stream/lab -d build_camera_usb_stream_mem -- -DEXTRA_CONF_FILE=prj_cdc.conf -DCONFIG_XIP=n -DDTS_EXTRA_CPPFLAGS="-DENC_MEMORY_INPUT"
```

이후는 다른 랩과 동일하게 `openocd_flash.py`로 플래시합니다.

## Step 2. 실행 & 확인

1. 보드를 리셋/전원 인가하면 콘솔에 다음과 같은 로그가 뜹니다(실측 로그 기준):
   ```
   *** Booting Zephyr OS build v4.4.1 ***
   [00:00:00.360,000] <inf> usb_stream_sample: USB CDC streaming lab start (live-to-memory, mode=0)
   [00:00:00.365,000] <inf> usb_stream_sample: enc video_set_format ret=0 capacity=131072 pitch=0 (480x270)
   [00:00:00.370,000] <inf> usb_stream_sample: enc video_get_caps ret=0 min_vbuf=1 align=64
   [00:00:00.374,000] <inf> usb_stream_sample: enc app_buf[0] addr=0x... size=131072 align=64
   [00:00:00.379,000] <inf> usb_stream_sample: enc video_enqueue[0] ret=0
   [00:00:00.383,000] <inf> usb_stream_sample: enc app_buf[1] addr=0x... size=131072 align=64
   [00:00:00.387,000] <inf> usb_stream_sample: enc video_enqueue[1] ret=0
   [00:00:00.405,000] <inf> usb_stream_sample: enc video_stream_start ret=0
   USB CDC ready; waiting for host DTR (connect + open the PC viewer)...
   ```
2. 보드를 USB 케이블로 PC에 연결하면 가상 COM 포트가 새로 열거됩니다(Windows 장치관리자에서 "Syna CDC ACM" 확인, 또는 WSL이면 `usbipd`로 바인딩 후 `/dev/ttyACM0` 확인 — Lab 01 문서의 USB/WSL 절차 재사용).
3. PC에서 뷰어 스크립트를 실행합니다:
   ```bash
   python recv_stream_cdc.py --port COM5        # Windows 예시
   # 또는
   python recv_stream_cdc.py --port /dev/ttyACM0   # WSL 예시
   ```
4. 뷰어가 포트를 열면(=DTR이 세팅되면) 보드 콘솔에 `Host DTR set - streaming started (mode=live-to-memory)` 로그가 뜨고, PC 화면에 카메라가 보는 장면이 실시간으로(수십 fps 수준, TFT 프리뷰보다 훨씬 빠름 — SPI가 아니라 USB라서) 갱신되기 시작해야 합니다.
5. `q` 키를 누르면 뷰어가 종료됩니다. 뷰어를 다시 실행하면(포트를 다시 열면) 보드가 자동으로 다시 스트리밍을 시작합니다.

## 확인용 체크리스트

- [x] `west build -b sr100_rdk/sr100/m55` 빌드 성공 (mode=0, `EXTRA_CONF_FILE=prj_cdc.conf`, `-DCONFIG_XIP=n`)
- [x] 보드가 USB CDC ACM(가상 COM 포트)로 정상 열거됨 (PC에서 포트 확인 가능)
- [x] `recv_stream_cdc.py` 실행 시 보드 콘솔에 `Host DTR set - streaming started` 로그가 뜸
- [x] PC 화면에 카메라 영상이 실시간으로(끊김 없이 계속) 갱신됨
- [x] 뷰어를 껐다 켜도(포트를 닫았다 다시 열어도) 스트리밍이 정상적으로 재개됨
- [ ] (선택) `mode=1`(예약메모리 컬러바 패턴)으로도 빌드해서 카메라 없이 USB 파이프라인 자체 동작 확인 (30프레임 후 자동 정지)

## 다음

이것으로 카메라 기초 실습 시리즈(Lab 01~05)를 마칩니다. 카메라를 활용한 AI(NPU 추론 등) 실습은 별도 커리큘럼에서 이어집니다.

빌드/실행 중 겪었던 문제와 해결 과정은 [`TROUBLESHOOTING_kr.md`](./TROUBLESHOOTING_kr.md)를 참고하세요.
