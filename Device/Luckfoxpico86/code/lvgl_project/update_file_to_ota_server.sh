#!/bin/bash

# Cấu hình đường dẫn
SOURCE_DIR="ota"
OTA_DIR="/var/www/ota_root/ota"

echo "=========================================="
echo "HỆ THỐNG CẬP NHẬT OTA TỰ ĐỘNG"
echo "=========================================="

# --- BƯỚC 1: TỰ ĐỘNG NHẬN DIỆN FILE ---

# Tìm file app_* cũ trong thư mục đích (nếu có)
OLD_APP=$(ls "$OTA_DIR" 2>/dev/null | grep "^app_" | head -n 1)

# Tìm file app_* mới nhất trong thư mục nguồn ota/
NEW_APP=$(ls -t "$SOURCE_DIR"/app_* 2>/dev/null | head -n 1 | xargs basename)

# Kiểm tra nếu không thấy file mới ở nguồn
if [ -z "$NEW_APP" ]; then
    echo "[✘] LỖI: Không tìm thấy file app_ mới nào trong thư mục '$SOURCE_DIR/'"
    exit 1
fi

# --- BƯỚC 2: HIỂN THỊ TRẠNG THÁI TRƯỚC KHI CHẠY ---

echo "TRẠNG THÁI DỰ KIẾN:"
if [ -n "$OLD_APP" ]; then
    echo "  [-] Sẽ xóa file cũ: $OLD_APP"
else
    echo "  [*] Không có file cũ (Bỏ qua bước xóa)"
fi
echo "  [+] Sẽ copy file mới: $NEW_APP"
echo "  [+] Sẽ copy file: check.json"
echo "------------------------------------------"

# --- BƯỚC 3: XÁC NHẬN ---

echo -n "Xác nhận thực hiện? (gõ 'ok' để chạy): "
read CONFIRM

if [ "$CONFIRM" != "ok" ]; then
    echo "Đã hủy bỏ."
    exit 0
fi

# --- BƯỚC 4: THỰC THI ---

echo "Đang xử lý..."

# Chỉ xóa file cũ nếu nó tồn tại
if [ -n "$OLD_APP" ]; then
    sudo rm -f "$OTA_DIR/$OLD_APP"
fi

# Xóa check.json cũ để đảm bảo không bị ghi đè lỗi
sudo rm -f "$OTA_DIR/check.json"

# Copy file mới
if [ -f "$SOURCE_DIR/$NEW_APP" ]; then
    sudo cp "$SOURCE_DIR/$NEW_APP" "$OTA_DIR/"
    sudo cp "$SOURCE_DIR/check.json" "$OTA_DIR/"
    
    # --- BƯỚC 5: HIỂN THỊ KẾT QUẢ CUỐI CÙNG ---
    echo "------------------------------------------"
    echo "✔ CẬP NHẬT HOÀN TẤT!"
    echo "Danh sách file hiện tại trong $OTA_DIR:"
    echo "------------------------------------------"
    # Liệt kê chi tiết: Quyền hạn, Chủ sở hữu, Kích thước, Ngày giờ, Tên file
    ls -lh "$OTA_DIR"
    echo "=========================================="
else
    echo "[✘] LỖI: File nguồn $SOURCE_DIR/$NEW_APP không tồn tại."
    exit 1
fi