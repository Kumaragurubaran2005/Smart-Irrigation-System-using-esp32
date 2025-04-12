const WebSocket = require('ws');
const wss = new WebSocket.Server({ port: 8080 });

// Track connected clients
const clients = {
    esp32: null,
    website: null
};

// Function to get current client status
function getClientStatus() {
    return {
        esp32: clients.esp32 !== null ? 'connected' : 'disconnected',
        website: clients.website !== null ? 'connected' : 'disconnected'
    };
}

// Function to log current status
function logStatus() {
    const status = getClientStatus();
    console.log('Current status:');
    console.log(`- ESP32: ${status.esp32}`);
    console.log(`- Website: ${status.website}`);
    console.log('----------------------');
}

wss.on('connection', (ws, req) => {
    const clientIp = req.socket.remoteAddress;
    console.log(`\nNew connection attempt from ${clientIp}`);

    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            
            // Client identification
            if (data.type === 'register') {
                if (data.client === 'esp32') {
                    if (clients.esp32) {
                        console.log('⚠️  ESP32 already connected! Rejecting new connection.');
                        ws.close(1008, 'ESP32 already connected');
                        return;
                    }
                    clients.esp32 = ws;
                    console.log('✅ ESP32 successfully registered!');
                    logStatus();
                } 
                else if (data.client === 'website') {
                    if (clients.website) {
                        console.log('⚠️  Website already connected! Rejecting new connection.');
                        ws.close(1008, 'Website already connected');
                        return;
                    }
                    clients.website = ws;
                    console.log('✅ Website successfully registered!');
                    logStatus();
                }
                return;
            }

            // Route messages
            if (ws === clients.esp32) {
                console.log(`📩 Received from ESP32: ${message}`);
                if (clients.website) {
                    clients.website.send(JSON.stringify({
                        type: 'sensor_data',
                        data: data
                    }));
                }
            }
            else if (ws === clients.website) {
                console.log(`📩 Received from Website: ${message}`);
                if (clients.esp32) {
                    clients.esp32.send(JSON.stringify(data));
                }
            }
        } catch (e) {
            console.error('❌ Error processing message:', e);
        }
    });

    ws.on('close', () => {
        if (ws === clients.esp32) {
            clients.esp32 = null;
            console.log('\n🛑 ESP32 disconnected!');
            logStatus();
        }
        else if (ws === clients.website) {
            clients.website = null;
            console.log('\n🛑 Website disconnected!');
            logStatus();
        } else {
            console.log(`\n🛑 Unknown client disconnected from ${clientIp}`);
        }
    });

    ws.on('error', (error) => {
        console.error('\n❌ WebSocket error:', error);
    });
});

console.log('\n🚀 WebSocket server running on ws://localhost:8080');
console.log('Waiting for connections...\n');