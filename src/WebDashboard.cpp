#include "WebDashboard.h"
#include <WiFi.h>
#include <WebServer.h>
#include "Config.h"

WebServer* server;

const char* ssid = "S3_GPS_Tracker";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>GPS Tracker Dashboard</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600;800&display=swap');
        
        * { box-sizing: border-box; margin: 0; padding: 0; }
        
        body {
            font-family: 'Inter', sans-serif;
            background: linear-gradient(135deg, #0f172a 0%, #1e1b4b 100%);
            color: #e2e8f0;
            height: 100vh;
            overflow: hidden;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }

        .dashboard {
            width: 100%;
            max-width: 450px;
            height: 100%;
            max-height: 800px;
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 24px;
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            padding: 30px 25px;
            display: flex;
            flex-direction: column;
            gap: 25px;
            box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.5);
        }

        .header {
            text-align: center;
            margin-bottom: 10px;
        }

        .header h1 {
            font-size: 24px;
            font-weight: 800;
            background: linear-gradient(to right, #38bdf8, #818cf8);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            margin-bottom: 5px;
        }

        .header p {
            font-size: 13px;
            color: #94a3b8;
        }

        .status-badge {
            align-self: center;
            padding: 8px 20px;
            border-radius: 50px;
            font-size: 14px;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 8px;
            transition: all 0.3s ease;
        }

        .status-badge.online { background: rgba(34, 197, 94, 0.2); color: #4ade80; border: 1px solid rgba(34, 197, 94, 0.3); }
        .status-badge.error { background: rgba(239, 68, 68, 0.2); color: #f87171; border: 1px solid rgba(239, 68, 68, 0.3); }
        .status-badge.warning { background: rgba(234, 179, 8, 0.2); color: #facc15; border: 1px solid rgba(234, 179, 8, 0.3); }

        .indicator {
            width: 10px;
            height: 10px;
            border-radius: 50%;
        }

        .status-badge.online .indicator { background: #4ade80; box-shadow: 0 0 10px #4ade80; }
        .status-badge.error .indicator { background: #f87171; box-shadow: 0 0 10px #f87171; }
        .status-badge.warning .indicator { background: #facc15; box-shadow: 0 0 10px #facc15; }

        .card-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            flex-grow: 1;
        }

        .card {
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid rgba(255, 255, 255, 0.05);
            border-radius: 16px;
            padding: 20px 15px;
            display: flex;
            flex-direction: column;
            justify-content: center;
            gap: 8px;
            transition: transform 0.2s;
        }

        .card:active { transform: scale(0.96); }

        .card-icon {
            font-size: 24px;
            margin-bottom: 5px;
        }

        .card-title {
            font-size: 12px;
            color: #94a3b8;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .card-value {
            font-size: 16px;
            font-weight: 600;
            word-break: break-all;
            line-height: 1.3;
        }

        .card.full-width {
            grid-column: span 2;
        }

        .value-highlight {
            color: #38bdf8;
        }

        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }

        .loading { animation: pulse 1.5s cubic-bezier(0.4, 0, 0.6, 1) infinite; }
    </style>
</head>
<body>

<div class="dashboard">
    <div class="header">
        <h1>S3 GPS Tracker</h1>
        <p>Live Device Telemetry</p>
    </div>

    <div id="main-status" class="status-badge warning">
        <div class="indicator"></div>
        <span id="state-text">Đang kết nối...</span>
    </div>

    <div class="card-grid">
        <div class="card full-width">
            <div class="card-title">Thiết bị ID (JT808)</div>
            <div class="card-value value-highlight" id="val-tid">Đang tải...</div>
        </div>
        
        <div class="card">
            <div class="card-icon">📡</div>
            <div class="card-title">Tọa độ Lat</div>
            <div class="card-value" id="val-lat">0.000000</div>
        </div>

        <div class="card">
            <div class="card-icon">🌍</div>
            <div class="card-title">Tọa độ Lon</div>
            <div class="card-value" id="val-lon">0.000000</div>
        </div>

        <div class="card full-width">
            <div class="card-title">IMEI Module</div>
            <div class="card-value" id="val-imei">Đang tải...</div>
        </div>

        <div class="card full-width">
            <div class="card-title">SIM CCID</div>
            <div class="card-value" id="val-ccid" style="font-size: 13px;">Đang tải...</div>
        </div>

        <div class="card full-width" style="flex-direction: row; justify-content: space-between; align-items: center;">
            <div class="card-title">Device Time</div>
            <div class="card-value" id="val-time" style="color: #facc15; font-size: 14px;">Đang tải...</div>
        </div>

        <div class="card full-width" style="flex-direction: row; justify-content: space-between; align-items: center;">
            <div class="card-title">Free Heap</div>
            <div class="card-value" id="val-heap" style="color: #4ade80;">0 KB</div>
        </div>
    </div>
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
                
                const stateCode = data.state;
                const stateText = states[stateCode] || "Không xác định";
                const badge = document.getElementById('main-status');
                const textSpan = document.getElementById('state-text');
                
                textSpan.innerText = stateText;
                
                badge.className = "status-badge";
                if (stateCode === 6) { // HAS_GPS
                    badge.classList.add('online');
                } else if (stateCode >= 2 && stateCode <= 4) { // Lỗi SIM, mạng
                    badge.classList.add('error');
                } else {
                    badge.classList.add('warning'); // Đang khởi tạo, dò GPS
                }
            })
            .catch(err => {
                document.getElementById('state-text').innerText = "Mất kết nối với thiết bị";
                document.getElementById('main-status').className = "status-badge error";
            });
    }

    setInterval(updateDashboard, 1000);
    updateDashboard();
</script>

</body>
</html>
)rawliteral";

void handleRoot() {
    server->send(200, "text/html", index_html);
}

void handleApiStatus() {
    String json = "{";
    json += "\"state\":" + String((int)currentState) + ",";
    json += "\"terminal_id\":\"" + terminal_id + "\",";
    json += "\"imei\":\"" + modem_imei + "\",";
    json += "\"ccid\":\"" + modem_ccid + "\",";
    json += "\"lat\":" + String(current_lat, 6) + ",";
    json += "\"lon\":" + String(current_lon, 6) + ",";
    json += "\"time\":\"" + current_time + "\",";
    json += "\"heap\":" + String(ESP.getFreeHeap());
    json += "}";
    
    server->send(200, "application/json", json);
}

void web_init() {
    server = new WebServer(80);
    WiFi.softAP(ssid);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("\n-I-AP IP address: ");
    Serial.println(IP);
    
    server->on("/", handleRoot);
    server->on("/api/status", handleApiStatus);
    server->begin();
    Serial.println("-I-HTTP server started");
}

void web_loop() {
    if (server) {
        server->handleClient();
    }
}
