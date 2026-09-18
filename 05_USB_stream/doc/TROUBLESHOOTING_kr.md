# Lab 05 트러블슈팅 — USB CDC로 PC에 실시간 스트리밍

실기 검증 과정에서 만난 두 가지 문제와 해결 방법을 정리합니다.

## 1. 빌드 시 플래시(FLASH) 영역 초과

**증상**: 기본 빌드 옵션으로 빌드하면 링크 단계에서 아래와 같은 에러로 빌드가 실패합니다.

```
.../ld.bfd: region `FLASH' overflowed by 11688 bytes
collect2: error: ld returned 1 exit status
ninja: build stopped: subcommand failed.
```

**원인**: 이 랩은 JPEG 인코더 코드(Lab 04) + USB 디바이스 스택(USBD) + CDC ACM 클래스 + 인터럽트 기반 UART 드라이버까지 한 번에 링크합니다. 지금까지의 카메라 랩 중 코드/심볼 크기가 가장 크고, 기본 설정(XIP, 즉 flash에서 코드를 직접 실행)으로는 이 크기가 SR110의 FLASH 실행 영역 예산을 넘습니다. Lab 03이 shell 기능 추가로 겪었던 플래시 초과([`03_tft_preview/doc/TROUBLESHOOTING_kr.md`](../../03_tft_preview/doc/TROUBLESHOOTING_kr.md) 참고)와 같은 종류의 문제이지만, 원인이 된 기능(USB 스택)의 크기가 더 커서 `CONFIG_SHELL_MINIMAL` 같은 국소적인 설정 조정만으로는 해결되지 않았습니다.

**해결**: 빌드 커맨드에 `-DCONFIG_XIP=n`을 추가합니다. 이 옵션은 코드를 flash에서 바로 실행(execute-in-place)하는 대신 부팅 시 RAM으로 복사해서 실행하도록 바꿔서, "실행 가능한 flash 영역"이 아니라 "RAM 용량" 기준으로 크기를 따지게 만듭니다 — RAM이 이 랩의 코드 크기를 감당할 수 있어서 이 방식으로 해결되었습니다.

```bash
west build -p always -b sr100_rdk/sr100/m55 -s <워크스페이스>/SR110_Zephyr_camera/05_usb_stream/lab -d build_camera_usb_stream -- -DEXTRA_CONF_FILE=prj_cdc.conf -DCONFIG_XIP=n
```

> 참고: XIP를 끄면 부팅 시 코드를 flash에서 RAM으로 복사하는 절차가 추가되므로 부팅 시간이 약간 늘어나고, RAM 사용량도 늘어납니다. 이 랩에서는 문제 없이 동작했지만, 코드 크기를 더 줄이는 대신 XIP를 유지하고 싶다면 `CONFIG_USBD_CDC_ACM_CLASS`/USB 스택 관련 Kconfig를 더 세밀하게 조정하거나, Lab 03처럼 불필요한 로깅/기능을 줄이는 접근도 가능합니다.

## 2. 실행 시 USB 컨트롤러 초기화 실패 (`udc_dwc2: Wait for AHB idle timeout`)

**증상**: 위 1번 문제를 `-DCONFIG_XIP=n`으로 해결하고 플래시한 뒤, 최초 실행에서 아래와 같은 런타임 에러가 발생했습니다.

```
*** Booting Zephyr OS build v4.4.1 ***
[00:00:00.360,000] <inf> usb_stream_sample: USB CDC streaming lab start (live-to-memory, mode=0)
[00:00:00.365,000] <inf> usb_stream_sample: enc video_set_format ret=0 capacity=131072 pitch=0 (480x270)
[00:00:00.370,000] <inf> usb_stream_sample: enc video_get_caps ret=0 min_vbuf=1 align=64
[00:00:00.374,000] <inf> usb_stream_sample: enc app_buf[0] addr=0x33ec4f80 size=131072 align=64
[00:00:00.379,000] <inf> usb_stream_sample: enc video_enqueue[0] ret=0
[00:00:00.383,000] <inf> usb_stream_sample: enc app_buf[1] addr=0x33ee4f80 size=131072 align=64
[00:00:00.387,000] <inf> usb_stream_sample: enc video_enqueue[1] ret=0
[00:00:00.405,000] <inf> usb_stream_sample: enc video_stream_start ret=0
[00:00:00.421,000] <err> udc_dwc2: Wait for AHB idle timeout, GRSTCTL 0x00000000
[00:00:00.425,000] <err> usbd_dev: Failed to enable controller
[00:00:00.429,000] <err> usb_stream_sample: USB CDC init failed: -5
[00:00:00.433,000] <inf> video_syna_sr100_enc: JPEG encoder stream stopped
[00:00:00.437,000] <inf> usb_stream_sample: USB CDC streaming lab finished ret=-5 after 77 ms
```

이후 재시도에서는 USB CDC ACM이 정상적으로 초기화되고 스트리밍까지 정상 동작하는 것이 확인되었습니다.

**원인**: USB DWC2 컨트롤러가 리셋 이후 AHB(내부 버스) idle 상태가 될 때까지 기다리다가 타임아웃(`GRSTCTL 0x00000000`)이 발생한 것으로, USB 컨트롤러 하드웨어 초기화 시점의 문제입니다. `-DCONFIG_XIP=n` 적용으로 부팅 절차(RAM 복사)와 초기화 타이밍이 달라진 것이 최초 1회의 초기화 실패와 연관이 있을 가능성이 있으나, 정확한 근본 원인은 확정하지 못했습니다 — 전원 인가 직후의 USB 컨트롤러 리셋 타이밍이 빡빡해서 가끔 실패할 수 있는 것으로 보이며, 이후 재시도들에서는 재현되지 않았습니다.

**해결(재현 시 대응)**: 이 에러가 뜨면 보드를 리셋하거나 전원을 껐다 켜서 다시 시도하면 됩니다 — 재시도 시 정상적으로 초기화되는 것을 확인했습니다. 이후 반복 테스트에서는 이 문제가 다시 나타나지 않았습니다. 만약 이 에러가 매번 재현된다면, USB 케이블/포트를 바꿔보거나, 보드 전원 인가 순서(외부 전원 → USB 케이블 연결 순서)를 바꿔보는 것도 시도해볼 만합니다.
