# SR110 Zephyr 카메라 실습 (SR110_Zephyr_camera)

Synaptics Astra **SR110** RDK 보드(`sr100_rdk/sr100/m55`, Cortex-M55 + Ethos-U55)에서 **MIPI 카메라를 Zephyr RTOS로 다루는 방법**을 처음부터 차례로 익히는 5단계 실습 모음입니다. NPU/AI 추론은 다루지 않고, "카메라에서 이미지를 받아와서 가공/표시/전송하는" 카메라 사용법 자체에만 집중합니다. 카메라를 활용한 AI(NPU 추론 등) 실습은 별도 커리큘럼에서 이어집니다.

## 실습 목록

| # | 실습 | 한 줄 요약 | 하드웨어 검증 |
|---|---|---|---|
| [01](./01_basic_capture/) | 카메라 프레임 캡처 기초 | Zephyr `video_api`로 10프레임 캡처, GDB 덤프로 이미지 확인 | ✅ 완료 |
| [02](./02_continuous_stats/) | 연속 캡처 + 밝기 통계 | 100프레임 연속 캡처하며 매 프레임 밝기(평균/최댓값/최솟값) 계산 | ✅ 완료 |
| [03](./03_tft_preview/) | 카메라 → TFT 실시간 프리뷰 | ST7789V3 TFT에 흑백 라이브 프리뷰 표시 (1초 간격, shell로 간격 조절) | ✅ 완료 |
| [04](./04_jpeg_encode/) | JPEG 하드웨어 인코딩 + TFT 스틸 프리뷰 | 하드웨어 JPEG 인코더로 압축 → 온보드에서 디코딩해 컬러 스틸 이미지로 표시 | ✅ 완료 |
| [05](./05_usb_stream/) | USB CDC로 PC에 실시간 스트리밍 | JPEG 프레임을 USB CDC ACM으로 PC에 계속 전송, 파이썬 뷰어로 실시간 표시 | ✅ 완료 |

각 실습은 이전 실습 없이도 단독으로 진행할 수 있도록 문서화되어 있지만, 01 → 05 순서대로 진행하면 개념이 누적되어 이해하기 쉽습니다.

## 디렉토리 구조

```
SR110_Zephyr_camera/
├── readme.md / readme_kr.md           (지금 보고 있는 이 문서)
├── 01_basic_capture/
│   ├── doc/
│   │   ├── LAB.md / LAB_kr.md         실습 설명 (영문/국문)
│   └── lab/                            빌드 가능한 Zephyr 애플리케이션 (CMakeLists.txt, prj.conf, src/ 등)
├── 02_continuous_stats/
│   ├── doc/{LAB.md, LAB_kr.md}
│   └── lab/
├── 03_tft_preview/
│   ├── doc/{LAB.md, LAB_kr.md, TROUBLESHOOTING.md, TROUBLESHOOTING_kr.md}
│   └── lab/
├── 04_jpeg_encode/
│   ├── doc/{LAB.md, LAB_kr.md}
│   └── lab/
└── 05_usb_stream/
    ├── doc/{LAB.md, LAB_kr.md, TROUBLESHOOTING.md, TROUBLESHOOTING_kr.md}
    └── lab/
```

- **`doc/`**: 실습 설명 문서. 영문(`LAB.md`)과 국문(`LAB_kr.md`)을 모두 제공합니다. 실기 검증 과정에서 실제로 발생했던 문제와 해결 방법이 있는 실습(03, 05)에는 `TROUBLESHOOTING.md`/`TROUBLESHOOTING_kr.md`가 추가로 있습니다.
- **`lab/`**: 실제로 빌드/플래시하는 Zephyr west 애플리케이션입니다. 각 실습 문서의 "Step 1. 빌드 및 플래시"에서 이 폴더를 빌드 소스 디렉토리(`-s` 옵션)로 지정합니다.

## 준비물 (공통)

- Synaptics Astra SR110 RDK 보드
- OV02C10 카메라 모듈 (보드 J23 커넥터에 연결)
- west CLI 기반 Zephyr 개발 환경 (WSL2 또는 PowerShell 네이티브), OpenOCD + `openocd_flash.py`
- 시리얼 터미널 (PuTTY / TeraTerm / screen / minicom)
- Lab 03/04만: ST7789V3 TFT 모듈 + 레벨쉬프터(TXS0108E 등), 외부 USB-TTL 어댑터
- Lab 05만: PC에서 열 수 있는 USB 포트, Python 3 + `pyserial`/`opencv-python`/`numpy`

각 실습별로 필요한 준비물과 배선은 해당 실습 `doc/LAB_kr.md`의 "준비물" 섹션에 자세히 정리되어 있습니다.

## 시작하는 방법

1. [`01_basic_capture/doc/LAB_kr.md`](./01_basic_capture/doc/LAB_kr.md)부터 순서대로 읽으며 진행하세요.
2. 각 실습 문서는 "이 실습의 목표 → 배워야 하는 것 → 준비물 → 개념 → Step 1(빌드/플래시) → Step 2(실행/확인) → 확인용 체크리스트 → 다음" 순서로 구성되어 있습니다.
3. 막히는 부분이 있다면 해당 실습에 `TROUBLESHOOTING_kr.md`가 있는지 먼저 확인하세요(03, 05).

## 이 저장소의 배경

이 저장소는 원래 SR110 기반 카메라+AI 통합 커리큘럼의 일부(`02_camera_capture`)로 작성되었던 카메라 기초 실습들을, "카메라 사용법 자체"를 독립적으로 익힐 수 있도록 별도 저장소로 분리한 것입니다. AI 커리큘럼 쪽에서는 이 저장소의 실습들을 선행 학습으로 간주하고, 카메라와 NPU를 결합하는 내용에 집중합니다.
