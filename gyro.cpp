#include "gyro.h"
#include "config.h"
#include <Wire.h>

// ============================================================
//   gyro.cpp : LSM6DSV16X (Qwiic, I2C on Wire1) 生 I2C 読み出し版
// ============================================================
// SparkFun ライブラリが Wire1 で期待通りに動かなかったため、
// WHO_AM_I チェック + 生レジスタ読み出しで実装。
// 物理軸基準:
//   基板を「ロゴが読める向き」で水平置きした状態を横持ちとする
//   X+ = 右、Y+ = 奥、Z+ = 上
// ------------------------------------------------------------

#define IMU_I2C_ADDR        0x6B    // SA0=VCC (SparkFun Qwiic 既定)
#define REG_WHO_AM_I        0x0F
#define REG_FIFO_CTRL4      0x0A    // [2:0]=fifo_mode (0=BYPASS)
#define REG_CTRL1           0x10    // [6:4]=OP_MODE_XL, [3:0]=ODR_XL
#define REG_CTRL2           0x11    // [6:4]=OP_MODE_G, [3:0]=ODR_G (ジャイロ)
#define REG_CTRL3_C         0x12    // BDU, IF_INC, SW_RESET
#define REG_CTRL6           0x15    // [3:0]=FS_G (ジャイロ フルスケール)
#define REG_CTRL8           0x17    // FS_XL, HP ref mode 等
#define REG_OUTX_L_G        0x22    // ジャイロ出力 X/Y/Z (各 16bit LE)
#define REG_OUTX_L_A        0x28
#define WHO_AM_I_EXPECTED   0x70
// CTRL1: OP_MODE=HP(000) + ODR=120Hz(0110) → 0x06
#define CTRL1_VAL           0x06
// CTRL2: ジャイロ OP_MODE=HP + ODR=120Hz → 0x06
#define CTRL2_VAL           0x06
// CTRL6: ジャイロ FS (回転検知に十分な範囲を確保)
#define CTRL6_VAL           0x03
// CTRL3_C: BDU=1 + IF_INC=1 → 0x44
//   BDU=1 で L/H 16bit の整合性を保証、IF_INC=1 でレジスタアドレス自動加算
#define CTRL3_C_VAL         0x44

static int16_t ax_mg, ay_mg, az_mg;
static bool    imu_ready = false;

static bool shake_flag;
static bool shake_active;

// ---- 縦持ち回転検知 (ジャイロ Z 軸を積算) ----
//   閾値 GYRO_ROT_DEADZONE / GYRO_ROT_THRESHOLD は config.h に集約
static int32_t rot_accum;
static bool    rot_armed;

static bool i2c_write_reg(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(IMU_I2C_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    return (Wire1.endTransmission() == 0);
}

static bool i2c_read_reg(uint8_t reg, uint8_t* out) {
    Wire1.beginTransmission(IMU_I2C_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom((uint8_t)IMU_I2C_ADDR, (uint8_t)1) != 1) return false;
    *out = Wire1.read();
    return true;
}

static bool i2c_read_bytes(uint8_t reg, uint8_t* buf, uint8_t n) {
    // Nano R4 の Wire1 はバーストリードで同一バイトを返す挙動があるため、
    // 1バイトずつ別トランザクション (STOP 区切り) で読む。
    for (uint8_t i = 0; i < n; i++) {
        Wire1.beginTransmission(IMU_I2C_ADDR);
        Wire1.write(reg + i);
        if (Wire1.endTransmission(true) != 0) return false;
        if (Wire1.requestFrom((uint8_t)IMU_I2C_ADDR, (uint8_t)1) != 1) return false;
        buf[i] = Wire1.read();
    }
    return true;
}

static bool gyro_try_init() {
    Wire1.begin();
    Wire1.setClock(100000);

    uint8_t who = 0;
    if (!i2c_read_reg(REG_WHO_AM_I, &who)) return false;
    if (who != WHO_AM_I_EXPECTED) return false;

    i2c_write_reg(REG_CTRL3_C,   CTRL3_C_VAL);
    i2c_write_reg(REG_FIFO_CTRL4, 0x00);
    i2c_write_reg(REG_CTRL8,     0x00);
    i2c_write_reg(REG_CTRL6,     CTRL6_VAL);   // ジャイロ FS
    i2c_write_reg(REG_CTRL2,     CTRL2_VAL);   // ジャイロ ON (120Hz)
    if (!i2c_write_reg(REG_CTRL1, CTRL1_VAL)) return false;

    return true;
}

void gyro_init() {
    ax_mg = ay_mg = 0;
    az_mg = 1000;
    shake_flag   = false;
    shake_active = false;
    rot_accum    = 0;
    rot_armed    = false;
    imu_ready    = gyro_try_init();
}

void gyro_update() {
    if (!imu_ready) {
        static uint16_t retry_cnt = 0;
        if ((retry_cnt++ % 60) == 0) {
            imu_ready = gyro_try_init();
        }
        if (!imu_ready) return;
    }

    // Nano R4 の Wire1 は I2C トランザクション間で受信バッファを
    // キャッシュする既知不具合があり、OUT レジスタの新値が届かず読みが静止する。
    // 毎フレーム end()/begin() でリセットすることで回避。
    Wire1.end();
    Wire1.begin();
    Wire1.setClock(100000);

    // ジャイロ(0x22-)+ 加速度(0x28-) を 12 バイト一括読み
    uint8_t buf[12];
    if (!i2c_read_bytes(REG_OUTX_L_G, buf, 12)) return;

    // ジャイロ Z 軸 (buf[4..5]) を縦持ち回転検知に積算
    int16_t gz = (int16_t)(buf[4] | (buf[5] << 8));
    if (rot_armed && (gz > GYRO_ROT_DEADZONE || gz < -GYRO_ROT_DEADZONE)) {
        rot_accum += gz;
    }

    // 加速度 (buf[6..11])
    int16_t raw_x = (int16_t)(buf[6]  | (buf[7]  << 8));
    int16_t raw_y = (int16_t)(buf[8]  | (buf[9]  << 8));
    int16_t raw_z = (int16_t)(buf[10] | (buf[11] << 8));

    // ±2g FS: 0.061 mg/LSB → raw * 61 / 1000 = mg
    ax_mg = (int16_t)((int32_t)raw_x * 61 / 1000);
    ay_mg = (int16_t)((int32_t)raw_y * 61 / 1000);
    az_mg = (int16_t)((int32_t)raw_z * 61 / 1000);

    // ---- シェイク検出 (ヒステリシス付き) ----
    long mag_sq  = (long)ax_mg * ax_mg + (long)ay_mg * ay_mg + (long)az_mg * az_mg;
    long hi_sq   = (long)IMU_SHAKE_THRESHOLD_MG * IMU_SHAKE_THRESHOLD_MG;
    int16_t lo   = (int16_t)((int32_t)IMU_SHAKE_THRESHOLD_MG * IMU_SHAKE_RELEASE_PCT / 100);
    long lo_sq   = (long)lo * lo;

    if (mag_sq > hi_sq) {
        if (!shake_active) {
            shake_flag   = true;
            shake_active = true;
        }
    } else if (mag_sq < lo_sq) {
        shake_active = false;
    }
}

static int8_t to_dir(int16_t mg) {
    if (mg >  IMU_TILT_THRESHOLD_MG) return +1;
    if (mg < -IMU_TILT_THRESHOLD_MG) return -1;
    return 0;
}

int8_t gyro_tilt_x(Orientation o) {
    return (o == ORIENT_VERTICAL) ? to_dir(ay_mg) : to_dir(ax_mg);
}

int8_t gyro_tilt_y(Orientation o) {
    return (o == ORIENT_VERTICAL) ? to_dir((int16_t)-ax_mg) : to_dir(ay_mg);
}

bool gyro_shake_consumed() {
    if (shake_flag) { shake_flag = false; return true; }
    return false;
}

void gyro_rotate_reset() {
    rot_accum = 0;
    rot_armed = true;
}

bool gyro_rotate_detected() {
    if (!rot_armed) return false;
    return (rot_accum > GYRO_ROT_THRESHOLD) || (rot_accum < -GYRO_ROT_THRESHOLD);
}

bool gyro_is_ready() { return imu_ready; }
