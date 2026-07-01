#pragma once

#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>S3 GPS App</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700;800&display=swap');
        
        * { box-sizing: border-box; margin: 0; padding: 0; }
        
        body {
            font-family: 'Inter', sans-serif;
            background: linear-gradient(135deg, #090d16 0%, #111827 100%);
            color: #f1f5f9;
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 10px;
        }

        /* App Container */
        .app-container {
            width: 100%;
            max-width: 420px;
            height: 680px;
            background: rgba(17, 24, 39, 0.7);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 32px;
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            display: flex;
            flex-direction: column;
            box-shadow: 0 30px 60px -15px rgba(0, 0, 0, 0.8);
            position: relative;
            overflow: hidden;
        }

        /* Fixed Top Header */
        .app-header {
            padding: 20px 24px 15px 24px;
            border-bottom: 1px solid rgba(255, 255, 255, 0.06);
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: rgba(17, 24, 39, 0.4);
        }
        .header-brand h1 {
            font-size: 20px;
            font-weight: 800;
            background: linear-gradient(to right, #38bdf8, #818cf8);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .header-brand p { font-size: 11px; color: #64748b; font-weight: 600; margin-top: 1px; }

        /* Status Badge */
        .status-badge {
            padding: 6px 12px;
            border-radius: 50px;
            font-size: 11px;
            font-weight: 700;
            display: flex;
            align-items: center;
            gap: 6px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            transition: all 0.3s ease;
        }
        .status-badge.online { background: rgba(34, 197, 94, 0.15); color: #4ade80; border: 1px solid rgba(34, 197, 94, 0.25); }
        .status-badge.error { background: rgba(239, 68, 68, 0.15); color: #f87171; border: 1px solid rgba(239, 68, 68, 0.25); }
        .status-badge.warning { background: rgba(234, 179, 8, 0.15); color: #facc15; border: 1px solid rgba(234, 179, 8, 0.25); }
        .indicator-dot { width: 8px; height: 8px; border-radius: 50%; }
        .status-badge.online .indicator-dot { background: #4ade80; box-shadow: 0 0 8px #4ade80; }
        .status-badge.error .indicator-dot { background: #f87171; box-shadow: 0 0 8px #f87171; }
        .status-badge.warning .indicator-dot { background: #facc15; box-shadow: 0 0 8px #facc15; }

        /* Active Screen Window */
        .app-body {
            flex-grow: 1;
            overflow-y: auto;
            padding: 20px 24px;
            /* Extra bottom padding to avoid bottom-nav overlap */
            padding-bottom: 90px;
        }

        /* Screen Visibility States */
        .app-screen {
            display: none;
            flex-direction: column;
            gap: 16px;
            animation: fadeIn 0.3s ease;
        }
        .app-screen.active { display: flex; }
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(8px); }
            to { opacity: 1; transform: translateY(0); }
        }

        /* Cards and Elements Grid */
        .card-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
        .card {
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid rgba(255, 255, 255, 0.04);
            border-radius: 16px;
            padding: 16px 14px;
            display: flex;
            flex-direction: column;
            gap: 6px;
            transition: transform 0.2s;
        }
        .card:active { transform: scale(0.98); }
        .card.full-width { grid-column: span 2; }
        .card-title { font-size: 10px; color: #64748b; font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; }
        .card-value { font-size: 15px; font-weight: 600; color: #e2e8f0; word-break: break-all; }
        .value-highlight { color: #38bdf8; font-size: 16px; }

        /* Forms in Config Screen */
        .form-group { display: flex; flex-direction: column; gap: 6px; }
        .form-group label { font-size: 12px; color: #94a3b8; font-weight: 600; }
        .form-input {
            background: rgba(0, 0, 0, 0.25);
            border: 1px solid rgba(255, 255, 255, 0.08);
            color: white;
            padding: 12px;
            border-radius: 12px;
            font-size: 14px;
            outline: none;
            transition: border-color 0.2s;
        }
        .form-input:focus { border-color: #38bdf8; }
        .btn-submit {
            background: linear-gradient(135deg, #3b82f6 0%, #1d4ed8 100%);
            border: none; color: white;
            padding: 14px; border-radius: 12px;
            font-size: 14px; font-weight: 700;
            cursor: pointer; margin-top: 10px;
            box-shadow: 0 4px 12px rgba(59, 130, 246, 0.3);
            transition: all 0.2s;
        }
        .btn-submit:active { transform: scale(0.98); }

        /* Log Screen */
        .log-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: -5px; }
        .log-btn-group { display: flex; gap: 6px; }
        .log-btn {
            padding: 6px 12px; border: none; border-radius: 8px;
            font-size: 11px; font-weight: 600; cursor: pointer; color: white;
            transition: background 0.2s;
        }
        .log-copy { background: #334155; }
        .log-copy:hover { background: #475569; }
        .log-down { background: #0284c7; }
        .log-down:hover { background: #0369a1; }
        .log-console {
            font-family: 'Courier New', monospace;
            font-size: 11px;
            background: rgba(0, 0, 0, 0.4);
            border-radius: 16px;
            padding: 14px;
            height: 380px;
            overflow-y: auto;
            line-height: 1.6;
            color: #94a3b8;
            border: 1px solid rgba(255, 255, 255, 0.05);
        }
        .log-tx { color: #38bdf8; }
        .log-rx { color: #4ade80; }

        /* FOTA Screen */
        .fota-card {
            background: rgba(245, 158, 11, 0.05);
            border: 1px solid rgba(245, 158, 11, 0.15);
            border-radius: 16px;
            padding: 20px;
            display: flex;
            flex-direction: column;
            gap: 14px;
        }
        .fota-card h2 { font-size: 16px; color: #f59e0b; font-weight: 700; text-align: center; }
        .fota-file-input {
            background: rgba(0, 0, 0, 0.2);
            border: 1px dashed rgba(245, 158, 11, 0.4);
            padding: 20px 10px;
            border-radius: 12px;
            color: #e2e8f0;
            text-align: center;
            font-size: 13px;
            width: 100%;
            cursor: pointer;
        }
        .btn-fota {
            background: #f59e0b;
            border: none; color: white;
            padding: 14px; border-radius: 12px;
            font-size: 14px; font-weight: 700;
            cursor: pointer;
            box-shadow: 0 4px 12px rgba(245, 158, 11, 0.3);
            transition: all 0.2s;
        }
        .btn-fota:active { transform: scale(0.98); }
        .progress-container {
            width: 100%; height: 8px;
            background: rgba(255, 255, 255, 0.08);
            border-radius: 10px; overflow: hidden;
            display: none;
        }
        .progress-bar { height: 100%; background: #f59e0b; width: 0%; transition: width 0.2s; }
        .fota-status { font-size: 12px; color: #94a3b8; text-align: center; font-weight: 600; }

        /* Fixed Bottom Nav Bar */
        .bottom-nav {
            position: absolute;
            bottom: 0; left: 0; right: 0;
            height: 70px;
            background: rgba(15, 23, 42, 0.9);
            border-top: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 0 0 32px 32px;
            display: flex;
            justify-content: space-around;
            align-items: center;
            padding: 0 8px;
            z-index: 100;
            backdrop-filter: blur(10px);
            -webkit-backdrop-filter: blur(10px);
        }
        .nav-item {
            background: none; border: none;
            color: #64748b;
            display: flex; flex-direction: column;
            align-items: center; gap: 4px;
            cursor: pointer; flex: 1;
            transition: all 0.2s;
            outline: none;
        }
        .nav-item.active { color: #38bdf8; }
        .nav-icon { font-size: 18px; }
        .nav-text { font-size: 9px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; }

        /* Scrollbar styling */
        ::-webkit-scrollbar { width: 5px; height: 5px; }
        ::-webkit-scrollbar-track { background: transparent; }
        ::-webkit-scrollbar-thumb { background: rgba(255, 255, 255, 0.1); border-radius: 10px; }

        /* Switch styling */
        .switch {
            position: relative;
            display: inline-block;
            width: 44px;
            height: 22px;
        }
        .switch input { 
            opacity: 0;
            width: 0;
            height: 0;
        }
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: rgba(255, 255, 255, 0.1);
            transition: .3s;
            border-radius: 22px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        .slider:before {
            position: absolute;
            content: "";
            height: 16px;
            width: 16px;
            left: 2px;
            bottom: 2px;
            background-color: #94a3b8;
            transition: .3s;
            border-radius: 50%;
        }
        input:checked + .slider {
            background-color: rgba(56, 189, 248, 0.2);
            border-color: #38bdf8;
        }
        input:checked + .slider:before {
            transform: translateX(22px);
            background-color: #38bdf8;
        }
        input:disabled + .slider {
            opacity: 0.4;
            cursor: not-allowed;
        }
        .toggle-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 8px 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.03);
        }
        .toggle-row:last-child {
            border-bottom: none;
        }
        .toggle-label {
            font-size: 13px;
            font-weight: 600;
            color: #e2e8f0;
        }
        .toggle-desc {
            font-size: 10px;
            color: #64748b;
            margin-top: 1px;
        }
    </style>
</head>
<body>

<div class="app-container">
    <!-- Top Bar -->
    <header class="app-header">
        <div class="header-brand">
            <h1>S3 GPS Tracker</h1>
            <p>Thiết bị giám sát trực tuyến</p>
        </div>
        <div id="main-status" class="status-badge warning">
            <div class="indicator-dot"></div>
            <span id="state-text">Đang tải...</span>
        </div>
    </header>

    <!-- App Screen Contents -->
    <main class="app-body">
        
        <!-- Screen 1: Dashboard -->
        <section id="screen-monitor" class="app-screen active">
            <div class="card-grid">
                <div class="card full-width">
                    <div class="card-title">Thiết bị ID (JT808)</div>
                    <div class="card-value value-highlight" id="val-tid">Đang tải...</div>
                </div>

                <div class="card">
                    <div class="card-title">Tốc độ</div>
                    <div class="card-value" style="color: #38bdf8;"><span id="val-speed">0.0</span> <span style="font-size: 10px; color:#64748b;">km/h</span></div>
                </div>

                <div class="card">
                    <div class="card-title">Hướng đi</div>
                    <div class="card-value" style="color: #38bdf8;"><span id="val-course">0.0</span><span style="font-size: 10px; color:#64748b;">°</span></div>
                </div>

                <div class="card">
                    <div class="card-title">Điện áp Pin VBAT</div>
                    <div class="card-value" style="color: #fb7185;"><span id="val-vbat">0.00</span> <span style="font-size: 10px; color:#64748b;">V</span></div>
                </div>

                <div class="card">
                    <div class="card-title">Heap RAM</div>
                    <div class="card-value" id="val-heap" style="color: #a78bfa;">0 KB</div>
                </div>
                
                <div class="card">
                    <div class="card-title">Tọa độ Lat (Vĩ độ)</div>
                    <div class="card-value" id="val-lat" style="color: #4ade80; font-size: 13px;">0.000000</div>
                </div>

                <div class="card">
                    <div class="card-title">Tọa độ Lon (Kinh độ)</div>
                    <div class="card-value" id="val-lon" style="color: #4ade80; font-size: 13px;">0.000000</div>
                </div>

                <div class="card">
                    <div class="card-title">Alarm Hex</div>
                    <div class="card-value" id="val-alarm-hex" style="font-family: monospace; font-size: 14px; color: #f87171;">00000000</div>
                </div>

                <div class="card">
                    <div class="card-title">Status Hex</div>
                    <div class="card-value" id="val-status-hex" style="font-family: monospace; font-size: 14px; color: #60a5fa;">00000000</div>
                </div>

                <div class="card full-width">
                    <div class="card-title" style="margin-bottom: 8px;">Mô phỏng Trạng thái & Cảnh báo</div>
                    
                    <div class="toggle-row">
                        <div>
                            <div class="toggle-label">ACC (Chìa khóa xe)</div>
                            <div class="toggle-desc">Trạng thái khóa điện động cơ</div>
                        </div>
                        <label class="switch">
                            <input type="checkbox" id="sw-acc" onchange="toggleAlarm('acc', this.checked)">
                            <span class="slider"></span>
                        </label>
                    </div>

                    <div class="toggle-row">
                        <div>
                            <div class="toggle-label">Sử dụng ACC Vật lý</div>
                            <div class="toggle-desc">Đọc trạng thái từ chân ACC vật lý</div>
                        </div>
                        <label class="switch">
                            <input type="checkbox" id="sw-acc-phys" onchange="toggleAlarm('acc_phys', this.checked)">
                            <span class="slider"></span>
                        </label>
                    </div>

                    <div class="toggle-row">
                        <div>
                            <div class="toggle-label">SOS Khẩn cấp</div>
                            <div class="toggle-desc">Cảnh báo SOS (Bit 0)</div>
                        </div>
                        <label class="switch">
                            <input type="checkbox" id="sw-sos" onchange="toggleAlarm('sos', this.checked)">
                            <span class="slider"></span>
                        </label>
                    </div>

                    <div class="toggle-row">
                        <div>
                            <div class="toggle-label">Lái xe mệt mỏi</div>
                            <div class="toggle-desc">Cảnh báo Fatigue (Bit 2)</div>
                        </div>
                        <label class="switch">
                            <input type="checkbox" id="sw-fatigue" onchange="toggleAlarm('fatigue', this.checked)">
                            <span class="slider"></span>
                        </label>
                    </div>

                    <div class="toggle-row">
                        <div>
                            <div class="toggle-label">Lỗi ăng ten GPS</div>
                            <div class="toggle-desc">GPS Antenna Fault (Bit 5)</div>
                        </div>
                        <label class="switch">
                            <input type="checkbox" id="sw-gps-ant" onchange="toggleAlarm('gps_ant', this.checked)">
                            <span class="slider"></span>
                        </label>
                    </div>

                    <div class="toggle-row">
                        <div>
                            <div class="toggle-label">Mất nguồn thiết bị</div>
                            <div class="toggle-desc">Power Cut (Bit 8)</div>
                        </div>
                        <label class="switch">
                            <input type="checkbox" id="sw-power-cut" onchange="toggleAlarm('power_cut', this.checked)">
                            <span class="slider"></span>
                        </label>
                    </div>

                    <div class="toggle-row">
                        <div>
                            <div class="toggle-label">Va chạm rung lắc</div>
                            <div class="toggle-desc">Collision Alarm (Bit 20)</div>
                        </div>
                        <label class="switch">
                            <input type="checkbox" id="sw-collision" onchange="toggleAlarm('collision', this.checked)">
                            <span class="slider"></span>
                        </label>
                    </div>
                </div>

                <div class="card full-width">
                    <div class="card-title">IMEI & SIM CCID</div>
                    <div class="card-value" style="font-size: 13px; color: #94a3b8;">
                        IMEI: <span id="val-imei" style="color: #e2e8f0;">Đang tải...</span><br/>
                        CCID: <span id="val-ccid" style="color: #e2e8f0; font-size: 12px;">Đang tải...</span>
                    </div>
                </div>

                <div class="card full-width" style="flex-direction: row; justify-content: space-between; align-items: center;">
                    <div class="card-title">Giờ thiết bị</div>
                    <div class="card-value" id="val-time" style="color: #facc15; font-size: 13px;">Đang tải...</div>
                </div>
            </div>
        </section>

        <!-- Screen 2: Config -->
        <section id="screen-config" class="app-screen">
            <div class="form-group">
                <label>Tên WiFi (SSID)</label>
                <input type="text" id="cfg-ssid" class="form-input">
            </div>
            <div class="form-group">
                <label>Mật Khẩu WiFi</label>
                <input type="text" id="cfg-pass" class="form-input">
            </div>
            <div class="form-group">
                <label>Server IP / Domain</label>
                <input type="text" id="cfg-ip" class="form-input">
            </div>
            <div class="form-group">
                <label>Server Port</label>
                <input type="number" id="cfg-port" class="form-input">
            </div>
            <div class="form-group">
                <label>Chu Kỳ Truyền (Giây)</label>
                <input type="number" id="cfg-interval" class="form-input">
            </div>
            <div class="form-group">
                <label>Giới Hạn Quá Tốc Độ (km/h)</label>
                <input type="number" id="cfg-overspeed" class="form-input" min="10" max="200" step="1">
            </div>
            <button class="btn-submit" onclick="saveSettings()">Lưu cấu hình & Khởi động lại</button>
        </section>

        <!-- Screen 3: Logs -->
        <section id="screen-logs" class="app-screen">
            <div class="log-header">
                <div class="card-title">Module AT Log History</div>
                <div class="log-btn-group">
                    <button class="log-btn log-copy" onclick="copyLogs()">Copy</button>
                    <button class="log-btn log-down" onclick="downloadLogs()">Tải về</button>
                </div>
            </div>
            <div id="log-console" class="log-console">Chưa có log...</div>
        </section>

        <!-- Screen 4: FOTA -->
        <section id="screen-fota" class="app-screen">
            <div class="fota-card">
                <h2>Cập Nhật Phần Mềm Mạch</h2>
                <div class="form-group">
                    <label>Chọn File Firmware (.bin)</label>
                    <input type="file" id="firmware-file" accept=".bin" class="fota-file-input">
                </div>
                <div class="progress-container" id="fota-bar-container">
                    <div class="progress-bar" id="fota-bar"></div>
                </div>
                <div class="fota-status" id="fota-status"></div>
                <button class="btn-fota" onclick="uploadFirmware()">🚀 Tiến hành cập nhật</button>
            </div>
        </section>

        <!-- Screen 5: Fingerprint -->
        <section id="screen-finger" class="app-screen">
            <div class="card-grid">
                <div class="card" id="fp-status-card">
                    <div class="card-title">Cảm biến</div>
                    <div class="card-value flex items-center gap-2">
                        <div class="pulse-dot" id="fp-status-dot" style="margin-right: 6px;"></div>
                        <span id="fp-status-text">Đang kết nối...</span>
                    </div>
                </div>
                <div class="card">
                    <div class="card-title">Số vân tay lưu</div>
                    <div class="card-value" id="fp-print-count">-- / 127</div>
                </div>
            </div>

            <div class="card full-width" style="padding: 16px 14px; display: flex; flex-direction: row; gap: 12px;">
                <button class="btn-submit" style="flex: 1; margin-top: 0; background: linear-gradient(135deg, #10b981 0%, #059669 100%); box-shadow: 0 4px 12px rgba(16, 185, 129, 0.3);" onclick="fpApiCall('/api/finger/add', 'POST')">➕ Thêm mới</button>
                <button class="btn-submit" style="flex: 1; margin-top: 0; background: linear-gradient(135deg, #6366f1 0%, #4f46e5 100%); box-shadow: 0 4px 12px rgba(99, 102, 241, 0.3);" onclick="fpApiCall('/api/finger/verify', 'POST')">🔍 Xác thực</button>
            </div>

            <div class="card full-width" style="padding: 16px 14px;">
                <div class="card-title">Xóa ID vân tay</div>
                <div class="input-row" style="display: flex; gap: 8px; margin-top: 8px;">
                    <input type="number" id="fp-delete-id" class="form-input" style="flex: 1; padding: 8px 12px; background: rgba(0,0,0,0.25); border: 1px solid rgba(255,255,255,0.08); border-radius: 8px; color: white;" placeholder="Nhập ID (1-4095)">
                    <button class="btn-submit" style="margin-top:0; padding: 8px 16px; border-radius: 8px; background: #ef4444;" onclick="fpDeleteEntry()">Xóa</button>
                </div>
            </div>

            <div class="card full-width" style="padding: 16px 14px; display: flex; flex-direction: row; justify-content: space-between; align-items: center;">
                <span class="card-title" style="color: #f87171;">Xóa toàn bộ cảm biến</span>
                <button class="btn-submit" style="margin-top: 0; padding: 8px 16px; border-radius: 8px; background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%);" onclick="fpConfirmClear()">Reset</button>
            </div>

            <!-- Finger Log Console -->
            <div class="log-header" style="margin-top: 8px;">
                <div class="card-title">Nhật ký vân tay</div>
                <button class="log-btn log-copy" onclick="fpClearLogs()">Xóa log</button>
            </div>
            <div id="fp-log-console" class="log-console" style="height: 180px; font-family: inherit; font-size: 12px; padding: 12px;">
                <div class="log info" style="color: rgba(56, 189, 248, 0.8);"># Sẵn sàng kết nối cảm biến...</div>
            </div>
        </section>

    </main>

    <!-- Bottom Navigation Bar -->
    <nav class="bottom-nav">
        <button class="nav-item active" onclick="switchTab('monitor')">
            <span class="nav-icon">📊</span>
            <span class="nav-text">Trạng thái</span>
        </button>
        <button class="nav-item" onclick="switchTab('config')">
            <span class="nav-icon">⚙️</span>
            <span class="nav-text">Cấu hình</span>
        </button>
        <button class="nav-item" onclick="switchTab('logs')">
            <span class="nav-icon">📋</span>
            <span class="nav-text">Log AT</span>
        </button>
        <button class="nav-item" onclick="switchTab('fota')">
            <span class="nav-icon">🚀</span>
            <span class="nav-text">FOTA</span>
        </button>
        <button class="nav-item" onclick="switchTab('finger')">
            <span class="nav-icon">🔑</span>
            <span class="nav-text">Vân tay</span>
        </button>
    </nav>
</div>

<script>
    const states = [
        "Khởi tạo", 
        "Phát nhạc khởi động", 
        "Lỗi: Không nhận SIM", 
        "Lỗi: Không có sóng", 
        "Lỗi: Không có 4G/GPRS", 
        "Đang dò GPS...", 
        "Hoạt động bình thường"
    ];

    let updateTimer = null;

    // Load initial settings data immediately on page load
    function loadConfigData() {
        fetch('/api/config')
            .then(res => res.json())
            .then(data => {
                document.getElementById('cfg-ssid').value = data.ssid || "";
                document.getElementById('cfg-pass').value = data.pass || "";
                document.getElementById('cfg-ip').value = data.ip || "";
                document.getElementById('cfg-port').value = data.port || 0;
                document.getElementById('cfg-interval').value = data.interval || 10;
                document.getElementById('cfg-overspeed').value = data.overspeed || 80;
            }).catch(()=>{});
    }

    function switchTab(tabId) {
        // Switch Active Class on Screens
        document.querySelectorAll('.app-screen').forEach(screen => {
            screen.classList.remove('active');
        });
        document.getElementById('screen-' + tabId).classList.add('active');

        // Switch Active Class on Nav Buttons
        document.querySelectorAll('.nav-item').forEach(item => {
            item.classList.remove('active');
        });
        
        // Find correct button based on click handler signature
        const navButtons = document.querySelectorAll('.nav-item');
        navButtons.forEach(btn => {
            if (btn.getAttribute('onclick').includes(tabId)) {
                btn.classList.add('active');
            }
        });
        
        // Scroll target log console to bottom when opening Log tab
        if (tabId === 'logs') {
            setTimeout(() => {
                const el = document.getElementById('log-console');
                el.scrollTop = el.scrollHeight;
            }, 50);
        }
    }

    function startDashboard() {
        updateTimer = setInterval(function() { 
            updateDashboard(); 
            // Only update logs if log tab is open to save CPU/Network bandwidth on S3
            if (document.getElementById('screen-logs').classList.contains('active')) {
                updateLogs();
            }
            // Update fingerprint status if finger tab is active
            if (document.getElementById('screen-finger').classList.contains('active')) {
                updateFpStatus();
            }
        }, 2000);
        updateDashboard();
        updateLogs();
        updateFpStatus();
        loadConfigData();
    }

    function updateDashboard() {
        fetch('/api/status')
            .then(response => response.json())
            .then(data => {
                document.getElementById('val-tid').innerText = data.terminal_id || "N/A";
                document.getElementById('val-imei').innerText = data.imei || "N/A";
                document.getElementById('val-ccid').innerText = data.ccid || "N/A";
                document.getElementById('val-lat').innerText = data.lat.toFixed(6);
                document.getElementById('val-lon').innerText = data.lon.toFixed(6);
                document.getElementById('val-time').innerText = data.time || "N/A";
                document.getElementById('val-heap').innerText = (data.heap / 1024).toFixed(1) + " KB";
                
                document.getElementById('val-speed').innerText = data.speed !== undefined ? data.speed.toFixed(1) : "0.0";
                document.getElementById('val-course').innerText = data.course !== undefined ? data.course.toFixed(1) : "0.0";
                document.getElementById('val-vbat').innerText = data.vbat !== undefined ? data.vbat.toFixed(2) : "0.00";
                document.getElementById('val-alarm-hex').innerText = data.alarm_hex || "00000000";
                document.getElementById('val-status-hex').innerText = data.status_hex || "00000000";

                document.getElementById('sw-acc').checked = data.status_acc;
                document.getElementById('sw-acc-phys').checked = data.use_phys_acc;
                document.getElementById('sw-acc').disabled = data.use_phys_acc;
                document.getElementById('sw-sos').checked = data.alarm_sos;
                document.getElementById('sw-fatigue').checked = data.alarm_fatigue;
                document.getElementById('sw-gps-ant').checked = data.alarm_gps_ant;
                document.getElementById('sw-power-cut').checked = data.alarm_power_cut;
                document.getElementById('sw-collision').checked = data.alarm_collision;
                
                const stateCode = data.state;
                const stateText = states[stateCode] || "Không xác định";
                const badge = document.getElementById('main-status');
                const textSpan = document.getElementById('state-text');
                
                textSpan.innerText = stateText;
                
                badge.className = "status-badge";
                if (stateCode === 6) { 
                    badge.classList.add('online');
                } else if (stateCode >= 2 && stateCode <= 4) { 
                    badge.classList.add('error');
                } else {
                    badge.classList.add('warning'); 
                }
            })
            .catch(err => {
                document.getElementById('state-text').innerText = "Mất kết nối";
                document.getElementById('main-status').className = "status-badge error";
            });
    }

    function toggleAlarm(name, checked) {
        const val = checked ? '1' : '0';
        const params = new URLSearchParams();
        params.append('name', name);
        params.append('value', val);
        fetch('/api/toggle_alarm', {
            method: 'POST',
            body: params
        }).then(async response => {
            const data = await response.json();
            if (!response.ok || !data.ok) throw new Error(data.msg || 'Không cập nhật được trạng thái');
            updateDashboard();
        }).catch(err => {
            alert(err.message);
            updateDashboard(); // Restore the authoritative device state.
        });
    }

    function saveSettings() {
        const ssid = document.getElementById('cfg-ssid').value;
        const pass = document.getElementById('cfg-pass').value;
        const ip = document.getElementById('cfg-ip').value;
        const port = document.getElementById('cfg-port').value;
        const interval = document.getElementById('cfg-interval').value;
        const overspeed = document.getElementById('cfg-overspeed').value;
        
        if(!ssid || !ip || !port || !interval || !overspeed) {
            alert("Vui lòng điền đầy đủ cấu hình bắt buộc!");
            return;
        }

        if(!confirm('Lưu cấu hình và khởi động lại thiết bị?')) return;

        const params = new URLSearchParams();
        params.append('ssid', ssid);
        params.append('pass', pass);
        params.append('ip', ip);
        params.append('port', port);
        params.append('interval', interval);
        params.append('overspeed', overspeed);

        fetch('/api/save', {
            method: 'POST',
            body: params
        }).then(async response => {
            const data = await response.json();
            if (!response.ok) throw new Error(data.message || 'Cấu hình không hợp lệ');
            alert("Đã lưu! Thiết bị đang khởi động lại...");
            setTimeout(() => location.reload(), 5000);
        }).catch((err) => {
            alert("Lưu cấu hình thất bại: " + err.message);
        });
    }

    function escapeHtml(t) { return t.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;'); }

    function updateLogs() {
        fetch('/api/logs').then(r=>r.json()).then(logs=>{
            const el=document.getElementById('log-console');
            if(!logs.length){el.innerHTML='<span style="color:#64748b">Chưa có log...</span>';return;}
            let h='';
            logs.forEach(l=>{
                const s=escapeHtml(l);
                h+=l.startsWith('TX:')?'<div class="log-tx">&gt; '+s+'</div>':'<div class="log-rx">&lt; '+s+'</div>';
            });
            el.innerHTML=h; el.scrollTop=el.scrollHeight;
        }).catch(()=>{});
    }

    function copyLogs() {
        fetch('/api/logs').then(r=>r.json()).then(logs=>{
            if(!logs.length){alert("Không có log để copy"); return;}
            navigator.clipboard.writeText(logs.join('\n')).then(()=>{
                alert("Đã copy log vào clipboard!");
            });
        }).catch(()=>{});
    }

    function downloadLogs() {
        fetch('/api/logs').then(r=>r.json()).then(logs=>{
            if(!logs.length){alert("Không có log để tải"); return;}
            const blob = new Blob([logs.join('\n')], { type: 'text/plain' });
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'modem_log.txt';
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            document.body.removeChild(a);
        }).catch(()=>{});
    }

    function uploadFirmware() {
        const fi=document.getElementById('firmware-file');
        const f=fi.files[0];
        if(!f){alert('Vui lòng chọn file firmware (.bin)!');return;}
        if(!confirm('Bắt đầu cập nhật firmware? Thiết bị sẽ tự động reboot.'))return;
        
        const fd=new FormData(); 
        fd.append('firmware', f, f.name);
        
        const xhr=new XMLHttpRequest();
        const bc=document.getElementById('fota-bar-container');
        const bar=document.getElementById('fota-bar');
        const st=document.getElementById('fota-status');
        
        bc.style.display='block'; 
        st.innerText='Đang tải lên...';
        
        xhr.upload.addEventListener('progress', function(e){
            if(e.lengthComputable){
                const p=Math.round((e.loaded/e.total)*100);
                bar.style.width=p+'%';
                st.innerText='Đang tải: '+p+'%';
            }
        });
        
        xhr.onload=function(){
            if(xhr.responseText==='OK'){
                st.innerText='Thành công! Thiết bị đang tự reboot...';
                bar.style.width='100%';
                setTimeout(()=>location.reload(), 6000);
            } else {
                st.innerText='Lỗi cập nhật: '+xhr.responseText;
                bc.style.display='none';
            }
        };
        
        xhr.onerror=function(){
            st.innerText='Mất kết nối khi tải firmware!';
            bc.style.display='none';
        };
        
        xhr.open('POST','/update'); 
        xhr.send(fd);
    }

    let lastFpStateStr = "";
    
    function fpLog(message, type = 'info') {
        const consoleBox = document.getElementById('fp-log-console');
        if (!consoleBox) return;
        const entry = document.createElement('div');
        const now = new Date();
        const timeStr = `${now.getHours().toString().padStart(2, '0')}:${now.getMinutes().toString().padStart(2, '0')}:${now.getSeconds().toString().padStart(2, '0')}`;
        
        let color = '#38bdf8'; // info
        if (type === 'success') color = '#4ade80';
        if (type === 'error') color = '#f87171';
        if (type === 'warning') color = '#facc15';
        
        entry.style.color = color;
        entry.style.padding = '3px 0';
        entry.style.borderBottom = '1px solid rgba(255,255,255,0.03)';
        entry.innerHTML = `<span style="opacity:0.4">[${timeStr}]</span> ${message}`;
        
        consoleBox.prepend(entry);
    }
    
    function fpClearLogs() {
        document.getElementById('fp-log-console').innerHTML = '<div style="color: rgba(255,255,255,0.4); font-size:11px;"># Da clear log.</div>';
    }
    
    function updateFpStatus() {
        fetch('/api/finger/status')
            .then(res => res.json())
            .then(d => {
                const textSpan = document.getElementById('fp-status-text');
                const dot = document.getElementById('fp-status-dot');
                const countEl = document.getElementById('fp-print-count');
                if (!textSpan || !dot || !countEl) return;
                
                if (d.sensorReady) {
                    const states = {
                        0: 'Ready', 1: 'Adding...', 2: 'Verifying...',
                        3: 'Deleting...', 4: 'Clearing...'
                    };
                    textSpan.innerText = states[d.state] || 'Ready';
                    textSpan.style.color = '#4ade80';
                    dot.style.background = d.state === 0 ? '#4ade80' : '#facc15';
                    dot.style.boxShadow = d.state === 0 ? '0 0 8px #4ade80' : '0 0 8px #facc15';
                } else {
                    textSpan.innerText = 'Ngoại tuyến';
                    textSpan.style.color = '#f87171';
                    dot.style.background = '#f87171';
                    dot.style.boxShadow = '0 0 8px #f87171';
                }
                
                countEl.innerText = `${d.fingerCount} / 127`;
                
                const currentFpStateStr = `${d.lastResult}|${d.state}`;
                if (currentFpStateStr !== lastFpStateStr) {
                    if (d.lastResult) fpLog(d.lastResult, d.lastResultType);
                    lastFpStateStr = currentFpStateStr;
                }
            })
            .catch(() => {
                const textSpan = document.getElementById('fp-status-text');
                if (textSpan) {
                    textSpan.innerText = 'Mat ket noi';
                    textSpan.style.color = '#f87171';
                }
            });
    }
    
    function fpApiCall(endpoint, method = 'GET') {
        const endpointLabel = endpoint.split('?')[0];
        
        if (method === 'POST') {
            if (endpointLabel === '/api/finger/add') fpLog('# Yeu cau them van tay moi...', 'info');
            if (endpointLabel === '/api/finger/verify') fpLog('# Yeu cau xac thuc van tay...', 'info');
            if (endpointLabel === '/api/finger/clearall') fpLog('# DANG XOA TOAN BO DU LIEU...', 'error');
        }
        
        fetch(endpoint, { method })
            .then(res => res.json())
            .then(data => {
                if (!data.ok && data.msg) fpLog(`Loi: ${data.msg}`, 'warning');
            })
            .catch(err => {
                fpLog(`Loi ket noi: ${err.message}`, 'error');
            });
    }
    
    function fpDeleteEntry() {
        const input = document.getElementById('fp-delete-id');
        const id = input.value;
        if (id === "" || id < 1 || id > 4095) {
            fpLog('Loi: ID van tay khong hop le (1-4095)', 'error');
            return;
        }
        fpLog(`# Yeu cau xoa van tay ID #${id}...`, 'warning');
        fpApiCall(`/api/finger/delete?id=${id}`, 'POST');
        input.value = '';
    }
    
    function fpConfirmClear() {
        if (confirm("CANH BAO: Ban co chac chan muon xoa toan bo van tay khoi cam bien?")) {
            fpApiCall('/api/finger/clearall', 'POST');
        }
    }

    startDashboard();
</script>
</body>
</html>
)rawliteral";
