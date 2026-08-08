#include "src/ui_helpers.h"
#include "src/config.h"

const lv_font_t* ui_get_font(int size) {
    app_config_t *cfg = config_get();
    int choice = cfg->font_choice;
    
    // 0: English (Montserrat), 1: Vietnamese (Roboto)
    if (choice == 1) {
        switch (size) {
            case 14: return &lv_font_roboto_14;
            case 16: return &lv_font_roboto_16;
            case 18: return &lv_font_roboto_18;
            case 20: return &lv_font_roboto_20;
            case 22: return &lv_font_roboto_22;
            case 24: return &lv_font_roboto_24;
            case 28: return &lv_font_roboto_28;
            case 32: return &lv_font_roboto_32;
            case 48: return &lv_font_roboto_48;
            default: return &lv_font_roboto_16;
        }
    } else {
        switch (size) {
            case 14: return &lv_font_montserrat_14;
            case 16: return &lv_font_montserrat_16;
            case 18: return &lv_font_montserrat_18;
            case 20: return &lv_font_montserrat_20;
            case 22: return &lv_font_montserrat_22;
            case 24: return &lv_font_montserrat_24;
            case 28: return &lv_font_montserrat_28;
            case 32: return &lv_font_montserrat_32;
            case 48: return &lv_font_montserrat_48;
            default: return &lv_font_montserrat_16;
        }
    }
}

#include <string.h>

const char* ui_get_text(const char* eng) {
    if (config_get()->font_choice == 0) return eng;

    if (strcmp(eng, "Room: %s°C") == 0) return "Phòng: %s°C";
    if (strcmp(eng, "Room: --°C") == 0) return "Phòng: --°C";
    if (strcmp(eng, "Cool") == 0) return "Làm mát";
    if (strcmp(eng, "Dry") == 0) return "Hút ẩm";
    if (strcmp(eng, "Heat") == 0) return "Sưởi ấm";
    if (strcmp(eng, "Fan Speed") == 0) return "Tốc độ quạt";
    if (strcmp(eng, "Eco") == 0) return "Tiết kiệm";
    if (strcmp(eng, "DND") == 0) return "Không làm phiền";
    if (strcmp(eng, "MMR") == 0) return "Dọn phòng";
    if (strcmp(eng, "Unoccupied") == 0) return "Trống";
    if (strcmp(eng, "Occupied") == 0) return "Đang có khách";
    if (strcmp(eng, "System Monitor") == 0) return "Trạng thái hệ thống";
    if (strcmp(eng, "CPU Usage") == 0) return "Mức dùng CPU";
    if (strcmp(eng, "RAM Usage") == 0) return "Mức dùng RAM";
    if (strcmp(eng, "IP Address") == 0) return "Địa chỉ IP";
    if (strcmp(eng, "Device Temp") == 0) return "Nhiệt độ TB";
    if (strcmp(eng, "Smart Room") == 0) return "Phòng thông minh";
    if (strcmp(eng, "Settings") == 0) return "Cài đặt";
    if (strcmp(eng, "Network") == 0) return "Mạng";
    if (strcmp(eng, "Display") == 0) return "Hiển thị";
    if (strcmp(eng, "System") == 0) return "Hệ thống";
    if (strcmp(eng, "Brightness") == 0) return "Độ sáng";
    if (strcmp(eng, "Language / Font") == 0) return "Ngôn ngữ / Font";
    if (strcmp(eng, "Update PIN Code") == 0) return "Cập nhật mã PIN";
    if (strcmp(eng, "Screen Timeout (seconds, 0 = never)") == 0) return "Tắt màn hình (giây, 0 = không tắt)";
    if (strcmp(eng, "Reboot System") == 0) return "Khởi động lại";
    if (strcmp(eng, "Apply & Save") == 0) return "Lưu & Áp dụng";
    if (strcmp(eng, "Back") == 0) return "Quay lại";
    if (strcmp(eng, "ENTER PIN") == 0) return "NHẬP MÃ PIN";
    if (strcmp(eng, "Cancel") == 0) return "Hủy bỏ";
    if (strcmp(eng, "Temperature") == 0) return "Nhiệt độ";
    if (strcmp(eng, "Temperature: %d°C") == 0) return "Nhiệt độ: %d°C";
    if (strcmp(eng, "Do Not Disturb") == 0) return "Không làm phiền";
    if (strcmp(eng, "Make My Room") == 0) return "Dọn phòng";
    if (strcmp(eng, "OTA Update Server URL") == 0) return "Địa chỉ máy chủ OTA";
    if (strcmp(eng, "System Update") == 0) return "Cập nhật hệ thống";
    if (strcmp(eng, "Check for Update") == 0) return "Kiểm tra cập nhật";
    if (strcmp(eng, "Current Version: %s") == 0) return "Phiên bản: %s";
    if (strcmp(eng, "Download & Install") == 0) return "Tải về & Cài đặt";
    if (strcmp(eng, "Installing...") == 0) return "Đang cài đặt...";
    if (strcmp(eng, "Update Successful") == 0) return "Cập nhật thành công";
    if (strcmp(eng, "Update Failed") == 0) return "Cập nhật thất bại";
    if (strcmp(eng, "Downloading... %d%%") == 0) return "Đang tải... %d%%";
    if (strcmp(eng, "Verifying...") == 0) return "Đang xác thực...";
    if (strcmp(eng, "WiFi SSID") == 0) return "Tên WiFi";
    if (strcmp(eng, "WiFi Password") == 0) return "Mật khẩu WiFi";
    if (strcmp(eng, "Connect WiFi") == 0) return "Kết nối WiFi";

    // BMS translations
    if (strcmp(eng, "Overview") == 0) return "Tong the";
    if (strcmp(eng, "ON") == 0) return "BAT";
    if (strcmp(eng, "OFF") == 0) return "TAT";
    if (strcmp(eng, "AIR CONDITIONING") == 0) return "MAY LANH";
    if (strcmp(eng, "LIGHTING") == 0) return "CHIEU SANG";
    if (strcmp(eng, "QUICK SCENES") == 0) return "KICH BAN NHANH";
    if (strcmp(eng, "Mode:") == 0) return "Che do:";
    if (strcmp(eng, "Fan speed:") == 0) return "Toc do:";
    if (strcmp(eng, "Auto") == 0) return "Tu dong";
    if (strcmp(eng, "Fan") == 0) return "Quat";
    if (strcmp(eng, "Low") == 0) return "Thap";
    if (strcmp(eng, "Med") == 0) return "TB";
    if (strcmp(eng, "High") == 0) return "Cao";
    if (strcmp(eng, "Open Store") == 0) return "Mo cua hang";
    if (strcmp(eng, "Close Store") == 0) return "Dong cua hang";
    if (strcmp(eng, "On") == 0) return "Dang bat";
    if (strcmp(eng, "Off") == 0) return "Da tat";
    if (strcmp(eng, "Auto Mode - Lux") == 0) return "Tu dong - Lux";
    if (strcmp(eng, "Manual Control") == 0) return "Dieu chinh thu cong";
    if (strcmp(eng, "Brightness:") == 0) return "Do sang:";
    if (strcmp(eng, "Color:") == 0) return "Mau sac:";
    if (strcmp(eng, "White") == 0) return "Trang";
    if (strcmp(eng, "Yellow") == 0) return "Vang";
    if (strcmp(eng, "Mix") == 0) return "Mix";
    if (strcmp(eng, "Auto Mode") == 0) return "Che do tu dong";
    if (strcmp(eng, "Room: %d*C") == 0) return "Phong: %d*C";
    if (strcmp(eng, "Avg: %s*C") == 0) return "TB: %s*C";
    if (strcmp(eng, "Avg: %s%%") == 0) return "TB: %s%%";
    if (strcmp(eng, "AUTO") == 0) return "TU DONG";
    if (strcmp(eng, "MAN") == 0) return "THU CONG";
    if (strcmp(eng, "SCHED") == 0) return "LICH";
    if (strcmp(eng, "Set:") == 0) return "Cai dat:";
    if (strcmp(eng, "Outdoor") == 0) return "Ngoai";
    if (strcmp(eng, "Avg") == 0) return "TB";

    return eng;
}
