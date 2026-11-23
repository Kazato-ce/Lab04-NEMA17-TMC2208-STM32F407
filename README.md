# Lab04 - Điều Khiển Động Cơ Bước NEMA 17 qua Driver TMC2208 bằng STM32F407 và Giao Tiếp USB CDC

Dự án thực hành môn **Thiết Kế Hệ Thống Nhúng (CE224.R12)** - Trường Đại học Công nghệ Thông tin (UIT - VNUHCM).  
Hệ thống sử dụng vi điều khiển **STM32F407** tạo chuỗi xung PWM độc lập qua **TIM5** và **TIM2** để điều khiển đồng thời 2 động cơ bước **NEMA 17** thông qua 2 driver **TMC2208**, nhận lệnh điều khiển thời gian thực và phản hồi qua giao thức **USB CDC (Virtual COM Port)**.

---

## 👥 Thông Tin Nhóm Thực Hiện
- **Lớp**: CE224.R12
- **Giảng viên hướng dẫn**: ThS. Trần Ngọc Đức
- **Danh sách thành viên**:
  1. **Nguyễn Đoàn Thanh Phong** - MSSV: `24521338`
  2. **Đặng Thế Tú** - MSSV: `24521904`
  3. **Hoàng Minh Quân** - MSSV: `24521428`

---

## 📌 Tính Năng Nổi Bật

- **Điều khiển 2 động cơ bước độc lập**:
  - Động cơ 1: Điều khiển tốc độ bằng xung PWM từ **TIM5_CH3 (PA2)**, hướng quay **DIR1 (PA1)**, kích hoạt **EN1 (PA0)**.
  - Động cơ 2: Điều khiển tốc độ bằng xung PWM từ **TIM2_CH1 (PA5)**, hướng quay **DIR2 (PA4)**, kích hoạt **EN2 (PA3)**.
  - Hai timer 32-bit độc lập cho phép thay đổi tần số xung tức thời mà không làm ảnh hưởng lẫn nhau.
- **Giao thức điều khiển USB CDC (Virtual COM Port)**:
  - Giao tiếp tốc độ cao với máy tính qua cổng USB OTG FS (Full Speed 12 Mbps, PA11: DM, PA12: DP).
  - Khung truyền nhận dạng chuỗi văn bản kết thúc bằng ký tự xuống dòng (`\r` hoặc `\n`).
  - Hỗ trợ các lệnh: Quay thuận (`F`), quay ngược (`R`), dừng khẩn cấp giữ vị trí (`S`).
  - Phản hồi xác nhận trạng thái: `OK M<id> <hướng> <phần trăm>` hoặc báo lỗi cú pháp `ERR`.
- **Driver TMC2208 & Chế độ vi bước (Microstepping)**:
  - Cấu hình vi bước 1/8 (MS1, MS2 nối GND) cho chuyển động êm ái, chống ồn và hạn chế mất bước.
  - Tần số xung tối đa $F_{MAX} = 8000\text{ Hz}$ ứng với 100% tốc độ định mức.
  - Chân Enable (EN) tích cực mức thấp (Active Low): Khởi tạo mặc định mức 1 (tắt driver khi khởi động để chống nóng động cơ), kéo xuống mức 0 khi vận hành.

---

## ⚙️ Sơ Đồ Nối Dây Phần Cứng

### 1. Bảng kết nối tín hiệu STM32F407 <-> TMC2208

| Động cơ | Tín hiệu | Chân STM32F407 | Chân Driver TMC2208 | Chức năng |
| :--- | :--- | :--- | :--- | :--- |
| **Motor 1 (M1)** | `EN1` | `PA0` (GPIO Output) | `EN` | Bật/tắt driver (Mức 0: Bật, Mức 1: Tắt) |
| **Motor 1 (M1)** | `DIR1` | `PA1` (GPIO Output) | `DIR` | Chiều quay (Mức 1: Thuận, Mức 0: Ngược) |
| **Motor 1 (M1)** | `STEP1` | `PA2` (TIM5_CH3 PWM) | `STEP` | Xung bước điều khiển tốc độ |
| **Motor 2 (M2)** | `EN2` | `PA3` (GPIO Output) | `EN` | Bật/tắt driver (Mức 0: Bật, Mức 1: Tắt) |
| **Motor 2 (M2)** | `DIR2` | `PA4` (GPIO Output) | `DIR` | Chiều quay (Mức 1: Thuận, Mức 0: Ngược) |
| **Motor 2 (M2)** | `STEP2` | `PA5` (TIM2_CH1 PWM) | `STEP` | Xung bước điều khiển tốc độ |

### 2. Bảng kết nối nguồn & ngoại vi

| Kết nối | Từ | Đến | Ghi chú |
| :--- | :--- | :--- | :--- |
| **GND chung** | GND STM32 | GND Logic TMC2208 & GND Nguồn 12V | **Bắt buộc nối chung Mass** |
| **Nguồn Logic** | 3V3 STM32 | VIO của 2 Driver TMC2208 | Cấp nguồn cho khối logic TMC2208 |
| **Nguồn Công Suất** | Nguồn ngoài +12V (≥ 3A) | VM của 2 Driver TMC2208 | Mắc song song tụ hóa $\ge 100\mu\text{F}/25\text{V}$ sát chân VM-GND |
| **Động cơ bước** | Hai cuộn dây A, B NEMA 17 | Chân 1A/1B và 2A/2B của driver | Đấu đúng từng cặp cuộn dây |
| **Vi bước (Microstep)**| Chân MS1, MS2 TMC2208 | GND | Cấu hình vi bước 1/8 |
| **USB CDC** | Cổng USB OTG FS (`PA11` = DM, `PA12` = DP) | Cổng USB máy tính | Giao tiếp dòng lệnh Virtual COM Port |

---

## 📐 Nguyên Lý Tính Toán Tần Số Xung STEP

- Động cơ bước góc $1.8^\circ/\text{bước} \Rightarrow 200\text{ bước/vòng}$.
- Cấu hình vi bước $\mu = 8 \Rightarrow N = 200 \times 8 = 1600\text{ xung/vòng}$.
- Đặt tần số tối đa $F_{MAX} = 8000\text{ Hz}$ ứng với $100\%$ tốc độ:
  $$f_{STEP} = \frac{p}{100} \times F_{MAX} \quad (p \in [0, 100])$$
- Tốc độ quay lý thuyết của trục động cơ:
  $$n = \frac{f_{STEP} \times 60}{N} = \frac{f_{STEP} \times 60}{1600}\text{ (vòng/phút)}$$
- Cấu hình xung nhịp Timer:
  - Timer clock tick = $1\text{ MHz}$ ($1\mu\text{s}$ mỗi tick).
  - Tự động nạp giá trị: $\text{ARR} = \left(\frac{1\,000\,000}{f_{STEP}}\right) - 1$.
  - Duty cycle 50%: $\text{CCR} = \frac{\text{ARR} + 1}{2}$.

| Phần trăm $p$ (%) | Tần số $f_{STEP}$ (Hz) | Giá trị ARR | Tốc độ lý thuyết $n$ (vòng/phút) |
| :---: | :---: | :---: | :---: |
| **30%** | 2400 Hz | 416 | 90 rpm |
| **50%** | 4000 Hz | 249 | 150 rpm |
| **60%** | 4800 Hz | 207 | 180 rpm |
| **80%** | 6400 Hz | 155 | 240 rpm |
| **100%** | 8000 Hz | 124 | 300 rpm |

---

## 💻 Giao Thức Lệnh USB CDC

Máy tính gửi chuỗi ASCII qua Virtual COM Port, kết thúc bằng phím `Enter` (`\r` hoặc `\n`):

$$\text{M<id> <hướng> <phần trăm>}$$

- **`<id>`**: `1` (Động cơ 1 - PA0, PA1, PA2) hoặc `2` (Động cơ 2 - PA3, PA4, PA5).
- **`<hướng>`**:
  - `F` (Forward): Quay chiều thuận.
  - `R` (Reverse): Quay chiều ngược.
  - `S` (Stop): Dừng động cơ (ngừng xung PWM, duy trì lực giữ vị trí).
- **`<phần trăm>`**: Số nguyên từ `0` đến `100` (% tốc độ tối đa).

### Bảng ví dụ tập lệnh và phản hồi:

| Lệnh gửi | Mô tả chức năng | Phản hồi từ MCU |
| :--- | :--- | :--- |
| `M1 F 30` | Động cơ 1 quay thuận 30% tốc độ (2400 Hz) | `OK M1 F 30\r\n` |
| `M1 F 100` | Động cơ 1 quay thuận tối đa 100% (8000 Hz) | `OK M1 F 100\r\n` |
| `M1 R 60` | Động cơ 1 quay ngược 60% tốc độ (4800 Hz) | `OK M1 R 60\r\n` |
| `M1 S 0` | Động cơ 1 dừng khẩn cấp, giữ vị trí | `OK M1 S 0\r\n` |
| `M2 F 50` | Động cơ 2 quay thuận 50% tốc độ (4000 Hz) | `OK M2 F 50\r\n` |
| `M2 R 80` | Động cơ 2 quay ngược 80% tốc độ (6400 Hz) | `OK M2 R 80\r\n` |
| `M3 F 50` | Sai mã định danh ID (chỉ hỗ trợ M1 và M2) | `ERR\r\n` |

---

## 📂 Cấu Trúc Thư Mục Dự Án

```text
LAB04THUNGHIEMSTEP/
├── Core/
│   ├── Inc/
│   │   ├── main.h                 # Định nghĩa GPIO chân M1, M2 & prototype Timer
│   │   └── stm32f4xx_hal_conf.h   # Cấu hình các module HAL (TIM, USB, GPIO,...)
│   └── Src/
│       ├── main.c                 # Khởi tạo Clock 168MHz, TIM2/TIM5, GPIO & xử lý lệnh USB
│       ├── stm32f4xx_hal_msp.c    # Cấu hình phần cứng MSP (Clock TIM2/TIM5, Alternate Pins PA2, PA5)
│       ├── stm32f4xx_it.c         # Trình phục vụ ngắt ngoại vi (OTG_FS_IRQHandler, SysTick)
│       └── system_stm32f4xx.c     # Khởi tạo System Clock & Core
├── Drivers/                       # STM32F4xx HAL Driver & ARM CMSIS Core
├── Middlewares/                   # Thư viện ST STM32 USB Device Library (CDC Core & Class)
├── USB_DEVICE/                    # Cấu hình giao tiếp USB CDC (usbd_cdc_if, usbd_desc,...)
├── Video demo/                    # Chứa video quay thực nghiệm hoạt động của mạch
├── CMakeLists.txt                 # File cấu hình build CMake với bộ công cụ GCC ARM
├── STM32F407xx_FLASH.ld           # Linker Script (Heap: 0x400, Stack: 0x800)
├── LAB04THUNGHIEMSTEP.ioc         # File cấu hình STM32CubeMX hoàn chỉnh
├── Lab04-24521338_24521428_24521904.pdf # File Báo cáo thực hành chi tiết
└── README.md                      # Tài liệu hướng dẫn sử dụng dự án
```

---

## 🚀 Hướng Dẫn Biên Dịch & Nạp Chương Trình

### 1. Yêu cầu công cụ
- `arm-none-eabi-gcc` (phiên bản 10.3 hoặc mới hơn)
- `cmake` (>= 3.22) & `ninja`
- `ST-Link` / `OpenOCD` / `STM32CubeProgrammer`

### 2. Biên dịch bằng dòng lệnh
```bash
# Tạo thư mục build và cấu hình
cmake -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Tiến hành biên dịch chương trình
cmake --build build/Debug
```

Chương trình tạo file thực thi:
- ELF: `build/Debug/LAB04THUNGHIEMSTEP.elf`
- HEX: `build/Debug/LAB04THUNGHIEMSTEP.hex`
- BIN: `build/Debug/LAB04THUNGHIEMSTEP.bin`

### 3. Nạp vi điều khiển
- Nạp trực tiếp qua VS Code bằng tiện ích mở rộng **STM32 VS Code Extension** hoặc **Cortex-Debug**.
- Hoặc sử dụng công cụ nạp **STM32CubeProgrammer**: Chọn file `build/Debug/LAB04THUNGHIEMSTEP.hex` và nhấn **Download**.

---

## 📺 Video Demo Thực Nghiệm
Các video quay minh chứng thực tế điều khiển 2 động cơ bước NEMA 17 qua cổng USB CDC được lưu trữ trong thư mục:
👉 [`Video demo/`](./Video%20demo/)
