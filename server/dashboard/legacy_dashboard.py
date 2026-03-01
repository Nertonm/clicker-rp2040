"""
Dashboard HTTP/WebSocket para visualização do estado do jogo.

Arquitetura:
- HTTP server manual (asyncio.start_server) na porta 8080 para servir index.html
- WebSocket server (websockets lib) no path /ws para push de estado
- Broadcast loop a cada 0.5s para garantir < 1s de latência

Desacoplado do RPC: exceções aqui não afetam o dispatcher JSON-RPC.
"""

import asyncio
import json
import traceback
from websockets.asyncio.server import serve as ws_serve
from websockets.exceptions import ConnectionClosed

# Porta do dashboard (HTTP + WebSocket)
# Nota: WebSocket usa DASHBOARD_PORT + 1
DASHBOARD_PORT = 8080

# Conjunto de clientes WebSocket conectados
WS_CLIENTS = set()

# Referências injetadas pelo rpc_server
_game_manager = None
_node_registry = None
_get_active_connections = None

# HTML/CSS/JS do dashboard (embutido para evitar arquivos externos)
INDEX_HTML = r"""<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>~*+ Abacate Clicker +*~ Santuario Mistico</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Patrick+Hand&family=Indie+Flower&display=swap');

        * { box-sizing: border-box; margin: 0; padding: 0; }

        body {
            font-family: 'Patrick Hand', cursive, sans-serif;
            background: linear-gradient(135deg, #e8d5f2 0%, #d4e7d4 50%, #f5e6d3 100%);
            min-height: 100vh;
            color: #4a3728;
            position: relative;
            overflow-x: hidden;
        }

        /* Estrelinhas flutuantes */
        .stars {
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            pointer-events: none;
            z-index: 0;
            overflow: hidden;
        }
        .star {
            position: absolute;
            font-size: 20px;
            animation: twinkle 2s ease-in-out infinite, floaty 6s ease-in-out infinite;
            opacity: 0.6;
        }
        @keyframes twinkle {
            0%, 100% { opacity: 0.4; transform: scale(1); }
            50% { opacity: 1; transform: scale(1.2); }
        }
        @keyframes floaty {
            0%, 100% { transform: translateY(0); }
            50% { transform: translateY(-10px); }
        }

        /* Container principal */
        .container {
            max-width: 1000px;
            margin: 0 auto;
            padding: 20px;
            position: relative;
            z-index: 1;
        }

        /* ====== HEADER ====== */
        header {
            text-align: center;
            padding: 30px 20px;
            background: linear-gradient(180deg, rgba(255,255,255,0.8) 0%, rgba(255,255,255,0.3) 100%);
            border-radius: 30px;
            border: 3px dashed #b8a9c9;
            margin-bottom: 25px;
            position: relative;
        }

        .avocado-logo {
            width: 120px;
            height: 150px;
            margin: 0 auto 15px;
            animation: bounce 3s ease-in-out infinite;
        }
        @keyframes bounce {
            0%, 100% { transform: translateY(0) rotate(-3deg); }
            50% { transform: translateY(-10px) rotate(3deg); }
        }

        h1 {
            font-family: 'Indie Flower', cursive;
            font-size: 2.8rem;
            color: #6b8e5a;
            text-shadow: 2px 2px 0 #fff, 4px 4px 0 #d4b896;
            letter-spacing: 2px;
        }
        h1::before { content: '~*+ '; color: #c9a0dc; }
        h1::after { content: ' +*~'; color: #c9a0dc; }

        .subtitle {
            font-size: 1.1rem;
            color: #8b7355;
            margin-top: 5px;
        }
        .subtitle::before, .subtitle::after { content: ' ☆ '; color: #e6b980; }

        /* Status de conexao */
        .connection-badge {
            position: absolute;
            top: 15px;
            right: 20px;
            padding: 8px 15px;
            border-radius: 20px;
            font-size: 0.85rem;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .connection-badge.online {
            background: linear-gradient(135deg, #c8e6c9, #a5d6a7);
            border: 2px solid #81c784;
            color: #2e7d32;
        }
        .connection-badge.offline {
            background: linear-gradient(135deg, #ffcdd2, #ef9a9a);
            border: 2px solid #e57373;
            color: #c62828;
        }
        .status-icon { font-size: 1.1rem; }

        /* ====== SCORE PRINCIPAL ====== */
        .score-shrine {
            background: linear-gradient(135deg, #fff9e6 0%, #fff0f5 50%, #f0fff0 100%);
            border: 4px solid #d4b896;
            border-radius: 30px;
            padding: 30px;
            text-align: center;
            margin-bottom: 25px;
            box-shadow: 0 8px 25px rgba(139, 115, 85, 0.2), inset 0 0 30px rgba(255,255,255,0.8);
            position: relative;
        }
        .score-shrine::before, .score-shrine::after {
            content: '✿';
            position: absolute;
            top: 15px;
            font-size: 1.5rem;
            color: #f8bbd9;
        }
        .score-shrine::before { left: 20px; }
        .score-shrine::after { right: 20px; }

        .score-label {
            font-size: 1.2rem;
            color: #8b7355;
            text-transform: uppercase;
            letter-spacing: 3px;
            margin-bottom: 10px;
        }

        .score-value {
            font-family: 'Indie Flower', cursive;
            font-size: 4.5rem;
            color: #6b8e5a;
            text-shadow: 3px 3px 0 #fff, 5px 5px 0 #c9dfc3;
            line-height: 1;
        }
        .score-value.pulse {
            animation: scorePulse 0.4s ease-out;
        }
        @keyframes scorePulse {
            0% { transform: scale(1); }
            50% { transform: scale(1.1); color: #f4a460; }
            100% { transform: scale(1); }
        }

        .score-decoration {
            margin-top: 15px;
            font-size: 1.5rem;
            color: #c9a0dc;
        }

        /* ====== GRID DE PAINEIS ====== */
        .panels {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }
        @media (max-width: 768px) {
            .panels { grid-template-columns: 1fr; }
        }

        .panel {
            background: rgba(255,255,255,0.85);
            border: 3px solid;
            border-radius: 25px;
            padding: 20px;
            box-shadow: 0 5px 15px rgba(0,0,0,0.1);
        }

        .panel-nodes { border-color: #a5d6a7; }
        .panel-events { border-color: #ce93d8; }

        .panel-header {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-bottom: 15px;
            padding-bottom: 10px;
            border-bottom: 2px dashed;
        }
        .panel-nodes .panel-header { border-color: #c8e6c9; }
        .panel-events .panel-header { border-color: #e1bee7; }

        .panel-icon { font-size: 1.5rem; }
        .panel-title {
            font-family: 'Indie Flower', cursive;
            font-size: 1.4rem;
            color: #5d4037;
        }

        /* ====== NODES ====== */
        .nodes-grid {
            display: flex;
            flex-direction: column;
            gap: 12px;
            max-height: 350px;
            overflow-y: auto;
        }

        .node-card {
            display: flex;
            align-items: center;
            gap: 12px;
            padding: 12px 15px;
            background: linear-gradient(135deg, #f5f5f5 0%, #fafafa 100%);
            border: 2px solid #ddd;
            border-radius: 15px;
            transition: all 0.2s ease;
        }
        .node-card:hover {
            transform: translateX(5px);
            box-shadow: 0 3px 10px rgba(0,0,0,0.1);
        }

        .node-card.active { border-color: #81c784; background: linear-gradient(135deg, #e8f5e9, #f1f8e9); }
        .node-card.syncing { border-color: #fff176; background: linear-gradient(135deg, #fffde7, #fff8e1); }
        .node-card.inactive { border-color: #bdbdbd; opacity: 0.7; }

        .node-avatar {
            width: 40px;
            height: 40px;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 1.3rem;
            flex-shrink: 0;
        }
        .node-card.active .node-avatar { background: #c8e6c9; }
        .node-card.syncing .node-avatar { background: #fff9c4; }
        .node-card.inactive .node-avatar { background: #e0e0e0; }

        .node-info { flex: 1; min-width: 0; }
        .node-name {
            font-weight: bold;
            color: #5d4037;
            font-size: 0.95rem;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        .node-status-text {
            font-size: 0.8rem;
            color: #888;
        }
        .node-card.active .node-status-text { color: #43a047; }
        .node-card.syncing .node-status-text { color: #f9a825; }

        .node-score {
            font-family: 'Indie Flower', cursive;
            font-size: 1.5rem;
            color: #6b8e5a;
            font-weight: bold;
        }

        /* ====== EVENTOS ====== */
        .events-list {
            display: flex;
            flex-direction: column;
            gap: 10px;
            max-height: 350px;
            overflow-y: auto;
        }

        .event-item {
            padding: 10px 15px;
            background: #fafafa;
            border-left: 4px solid #ce93d8;
            border-radius: 0 12px 12px 0;
            font-size: 0.9rem;
        }
        .event-item.rate-limited {
            border-left-color: #ff8a65;
            background: #fff3e0;
        }
        .event-item.rate-limited.shake {
            animation: shake 0.3s ease-out;
        }
        @keyframes shake {
            0%, 100% { transform: translateX(0); }
            25% { transform: translateX(-5px); }
            75% { transform: translateX(5px); }
        }

        .event-main {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 5px;
        }
        .event-node-name { color: #7b1fa2; font-weight: bold; }
        .event-clicks { color: #5d4037; }
        .event-badge {
            background: #ff8a65;
            color: white;
            padding: 2px 8px;
            border-radius: 10px;
            font-size: 0.7rem;
            font-weight: bold;
        }

        .event-meta {
            display: flex;
            justify-content: space-between;
            font-size: 0.8rem;
            color: #999;
        }
        .event-lamport { color: #9575cd; }
        .event-lamport::before { content: '⏱ '; }

        /* ====== FOOTER ====== */
        footer {
            margin-top: 25px;
            padding: 20px;
            background: rgba(255,255,255,0.7);
            border-radius: 25px;
            border: 3px dashed #d4b896;
            display: flex;
            justify-content: space-around;
            align-items: center;
            flex-wrap: wrap;
            gap: 20px;
        }

        .footer-stat {
            text-align: center;
        }
        .footer-stat-value {
            font-family: 'Indie Flower', cursive;
            font-size: 2.2rem;
            color: #7b1fa2;
        }
        .footer-stat-label {
            font-size: 0.85rem;
            color: #8b7355;
        }

        .node-summary {
            display: flex;
            gap: 15px;
        }
        .summary-item {
            display: flex;
            align-items: center;
            gap: 5px;
            font-size: 0.95rem;
        }
        .summary-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
        }
        .summary-dot.active { background: #81c784; }
        .summary-dot.syncing { background: #fff176; }
        .summary-dot.inactive { background: #bdbdbd; }

        .mystic-message {
            flex-basis: 100%;
            text-align: center;
            font-style: italic;
            color: #9575cd;
            padding-top: 15px;
            border-top: 2px dashed #e1bee7;
            font-size: 1rem;
        }
        .mystic-message::before { content: '🌙 '; }
        .mystic-message::after { content: ' 🌙'; }

        /* ====== EMPTY STATES ====== */
        .empty-state {
            text-align: center;
            padding: 30px;
            color: #aaa;
        }
        .empty-state-icon { font-size: 3rem; margin-bottom: 10px; }

        /* ====== SCROLLBAR FOFA ====== */
        ::-webkit-scrollbar { width: 8px; }
        ::-webkit-scrollbar-track { background: #f5f5f5; border-radius: 10px; }
        ::-webkit-scrollbar-thumb { background: #ce93d8; border-radius: 10px; }
        ::-webkit-scrollbar-thumb:hover { background: #ba68c8; }
    </style>
</head>
<body>
    <!-- Estrelinhas de fundo -->
    <div class="stars" id="stars"></div>

    <div class="container">
        <header>
            <div class="connection-badge offline" id="connection-badge">
                <span class="status-icon" id="status-icon">💔</span>
                <span id="status-text">Desconectado</span>
            </div>

            <!-- Logo do Abacate fofo -->
            <svg class="avocado-logo" viewBox="0 0 100 130">
                <!-- Corpo do abacate -->
                <ellipse cx="50" cy="70" rx="40" ry="55" fill="#8bc34a"/>
                <ellipse cx="50" cy="70" rx="32" ry="45" fill="#c5e1a5"/>
                <!-- Caroco com carinha -->
                <circle cx="50" cy="80" r="18" fill="#8d6e63"/>
                <!-- Olhinhos -->
                <ellipse cx="44" cy="76" rx="3" ry="4" fill="#5d4037"/>
                <ellipse cx="56" cy="76" rx="3" ry="4" fill="#5d4037"/>
                <!-- Brilho nos olhos -->
                <circle cx="45" cy="75" r="1.5" fill="white"/>
                <circle cx="57" cy="75" r="1.5" fill="white"/>
                <!-- Bochecha -->
                <ellipse cx="38" cy="82" rx="4" ry="2.5" fill="#ffccbc" opacity="0.7"/>
                <ellipse cx="62" cy="82" rx="4" ry="2.5" fill="#ffccbc" opacity="0.7"/>
                <!-- Sorriso -->
                <path d="M 44 86 Q 50 92 56 86" stroke="#5d4037" stroke-width="2" fill="none" stroke-linecap="round"/>
                <!-- Folhinha -->
                <ellipse cx="50" cy="18" rx="8" ry="15" fill="#66bb6a" transform="rotate(-15 50 18)"/>
                <line x1="50" y1="18" x2="50" y2="30" stroke="#558b2f" stroke-width="2"/>
                <!-- Aureola mistica -->
                <ellipse cx="50" cy="8" rx="20" ry="6" fill="none" stroke="#fff59d" stroke-width="2" opacity="0.8"/>
            </svg>

            <h1>Abacate Clicker</h1>
            <p class="subtitle">Santuario dos Cliques Misticos</p>
        </header>

        <!-- Score Principal -->
        <div class="score-shrine">
            <div class="score-label">✧ Pontuacao Cosmica ✧</div>
            <div class="score-value" id="global-score">0</div>
            <div class="score-decoration">
                ･ﾟ✧ <span id="score-stars">☆</span> ✧ﾟ･
            </div>
        </div>

        <!-- Paineis -->
        <div class="panels">
            <!-- Painel de Nodes -->
            <div class="panel panel-nodes">
                <div class="panel-header">
                    <span class="panel-icon">🥑</span>
                    <span class="panel-title">Abacatinhos Conectados</span>
                </div>
                <div class="nodes-grid" id="nodes-list">
                    <div class="empty-state">
                        <div class="empty-state-icon">🥑💤</div>
                        <p>Nenhum abacatinho acordou ainda...</p>
                    </div>
                </div>
            </div>

            <!-- Painel de Eventos -->
            <div class="panel panel-events">
                <div class="panel-header">
                    <span class="panel-icon">📜</span>
                    <span class="panel-title">Pergaminho dos Cliques</span>
                </div>
                <div class="events-list" id="events-list">
                    <div class="empty-state">
                        <div class="empty-state-icon">🔮</div>
                        <p>Esperando as profecias...</p>
                    </div>
                </div>
            </div>
        </div>

        <!-- Footer -->
        <footer>
            <div class="footer-stat">
                <div class="footer-stat-value" id="active-connections">0</div>
                <div class="footer-stat-label">Conexoes Ativas</div>
            </div>

            <div class="node-summary">
                <div class="summary-item">
                    <span class="summary-dot active"></span>
                    <span><span id="count-active">0</span> ativos</span>
                </div>
                <div class="summary-item">
                    <span class="summary-dot syncing"></span>
                    <span><span id="count-syncing">0</span> sincronizando</span>
                </div>
                <div class="summary-item">
                    <span class="summary-dot inactive"></span>
                    <span><span id="count-inactive">0</span> dormindo</span>
                </div>
            </div>

            <div class="mystic-message" id="mystic-message">
                Os abacates guardam a sabedoria dos cliques antigos...
            </div>
        </footer>
    </div>

    <script>
        // ===== Criar estrelinhas de fundo =====
        const starsContainer = document.getElementById('stars');
        const starSymbols = ['✦', '✧', '⋆', '★', '☆', '✶', '✷', '✸'];
        for (let i = 0; i < 30; i++) {
            const star = document.createElement('span');
            star.className = 'star';
            star.textContent = starSymbols[Math.floor(Math.random() * starSymbols.length)];
            star.style.left = Math.random() * 100 + '%';
            star.style.top = Math.random() * 100 + '%';
            star.style.animationDelay = Math.random() * 3 + 's';
            star.style.color = ['#c9a0dc', '#f8bbd9', '#fff59d', '#b2dfdb'][Math.floor(Math.random() * 4)];
            starsContainer.appendChild(star);
        }

        // ===== Elementos DOM =====
        const el = {
            connectionBadge: document.getElementById('connection-badge'),
            statusIcon: document.getElementById('status-icon'),
            statusText: document.getElementById('status-text'),
            globalScore: document.getElementById('global-score'),
            scoreStars: document.getElementById('score-stars'),
            nodesList: document.getElementById('nodes-list'),
            eventsList: document.getElementById('events-list'),
            activeConnections: document.getElementById('active-connections'),
            countActive: document.getElementById('count-active'),
            countSyncing: document.getElementById('count-syncing'),
            countInactive: document.getElementById('count-inactive'),
            mysticMessage: document.getElementById('mystic-message')
        };

        let prevScore = 0;
        let prevEventIds = new Set();

        // ===== Mensagens misticas =====
        const messages = [
            "Os abacates guardam a sabedoria dos cliques antigos...",
            "Cada clique planta uma semente no jardim cosmico.",
            "A polpa do destino conecta todas as sementes.",
            "Sincronize seu coracao com o ritmo do abacate.",
            "O caroco guarda memorias de mil cliques.",
            "Entre a casca e a polpa, habita a magia.",
            "Os astros alinham os timestamps sagrados.",
            "Que o guacamole esteja com voce!"
        ];

        function rotateMessage() {
            el.mysticMessage.textContent = messages[Math.floor(Math.random() * messages.length)];
        }

        // ===== Utilitarios =====
        function formatTime(ts) {
            if (!ts) return '--:--';
            return new Date(ts * 1000).toLocaleTimeString('pt-BR', {hour: '2-digit', minute: '2-digit'});
        }

        function formatNodeId(id) {
            if (!id) return '???';
            return id.length > 18 ? id.slice(0, 18) + '...' : id;
        }

        function getStatusClass(status) {
            const s = (status || '').toUpperCase();
            if (s === 'ACTIVE') return 'active';
            if (s === 'SYNCING') return 'syncing';
            return 'inactive';
        }

        function getScoreStars(score) {
            if (score >= 10000) return '★★★★★';
            if (score >= 5000) return '★★★★☆';
            if (score >= 1000) return '★★★☆☆';
            if (score >= 500) return '★★☆☆☆';
            if (score >= 100) return '★☆☆☆☆';
            return '☆☆☆☆☆';
        }

        // ===== Renderizacao =====
        function renderNodes(nodes) {
            if (!nodes || nodes.length === 0) {
                el.nodesList.innerHTML = `
                    <div class="empty-state">
                        <div class="empty-state-icon">🥑💤</div>
                        <p>Nenhum abacatinho acordou ainda...</p>
                    </div>`;
                return;
            }

            el.nodesList.innerHTML = nodes.map(n => {
                const statusClass = getStatusClass(n.status);
                const statusEmoji = statusClass === 'active' ? '🌟' : statusClass === 'syncing' ? '⏳' : '💤';
                const statusText = statusClass === 'active' ? 'Ativo' : statusClass === 'syncing' ? 'Sincronizando' : 'Dormindo';

                return `
                    <div class="node-card ${statusClass}">
                        <div class="node-avatar">${statusEmoji}</div>
                        <div class="node-info">
                            <div class="node-name" title="${n.node_id}">${formatNodeId(n.node_id)}</div>
                            <div class="node-status-text">${statusText} · ${formatTime(n.last_seen)}</div>
                        </div>
                        <div class="node-score">${(n.score || 0).toLocaleString('pt-BR')}</div>
                    </div>`;
            }).join('');
        }

        function renderEvents(events) {
            if (!events || events.length === 0) {
                el.eventsList.innerHTML = `
                    <div class="empty-state">
                        <div class="empty-state-icon">🔮</div>
                        <p>Esperando as profecias...</p>
                    </div>`;
                return;
            }

            const newIds = new Set(events.map(e => e.id));

            el.eventsList.innerHTML = events.map(e => {
                const isLimited = e.rate_limited && e.rate_limited !== 0;
                const isNew = !prevEventIds.has(e.id);
                const shakeClass = isNew && isLimited ? 'shake' : '';

                return `
                    <div class="event-item ${isLimited ? 'rate-limited' : ''} ${shakeClass}">
                        <div class="event-main">
                            <span>
                                <span class="event-node-name">${formatNodeId(e.node_id)}</span>
                                <span class="event-clicks">${e.accepted}/${e.sent} cliques</span>
                            </span>
                            ${isLimited ? '<span class="event-badge">RATE LIMITED</span>' : ''}
                        </div>
                        <div class="event-meta">
                            <span class="event-lamport">Lamport ${e.lamport_ts}</span>
                            <span>${formatTime(e.created_at)}</span>
                        </div>
                    </div>`;
            }).join('');

            prevEventIds = newIds;
        }

        function updateCounts(nodes) {
            let active = 0, syncing = 0, inactive = 0;
            (nodes || []).forEach(n => {
                const s = (n.status || '').toUpperCase();
                if (s === 'ACTIVE') active++;
                else if (s === 'SYNCING') syncing++;
                else inactive++;
            });
            el.countActive.textContent = active;
            el.countSyncing.textContent = syncing;
            el.countInactive.textContent = inactive;
        }

        // ===== Update principal =====
        function updateDashboard(data) {
            const score = data.global_score || 0;

            if (score !== prevScore) {
                el.globalScore.textContent = score.toLocaleString('pt-BR');
                el.globalScore.classList.remove('pulse');
                void el.globalScore.offsetWidth;
                el.globalScore.classList.add('pulse');
                el.scoreStars.textContent = getScoreStars(score);
                prevScore = score;
            }

            el.activeConnections.textContent = data.active_connections || 0;

            renderNodes(data.nodes);
            updateCounts(data.nodes);
            renderEvents(data.events);
        }

        // ===== WebSocket =====
        function connect() {
            const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsPort = parseInt(location.port || 8080) + 1;
            const ws = new WebSocket(`${protocol}//${location.hostname}:${wsPort}`);

            ws.onopen = () => {
                el.connectionBadge.className = 'connection-badge online';
                el.statusIcon.textContent = '💚';
                el.statusText.textContent = 'Conectado';
            };

            ws.onclose = () => {
                el.connectionBadge.className = 'connection-badge offline';
                el.statusIcon.textContent = '💔';
                el.statusText.textContent = 'Desconectado';
                setTimeout(connect, 2000);
            };

            ws.onerror = () => ws.close();

            ws.onmessage = (event) => {
                try {
                    updateDashboard(JSON.parse(event.data));
                } catch (e) {
                    console.error('Parse error:', e);
                }
            };
        }

        // ===== Init =====
        connect();
        setInterval(rotateMessage, 8000);
    </script>
</body>
</html>
"""


def init_dashboard(game_manager, node_registry, get_active_connections):
    """
    Inicializa as referências necessárias para o dashboard.
    Deve ser chamado antes de start_servers().

    Args:
        game_manager: Instância do GameManager (para global_score)
        node_registry: Instância do NodeRegistry (para status dos nós)
        get_active_connections: Callable que retorna o número de conexões RPC ativas
    """
    global _game_manager, _node_registry, _get_active_connections
    _game_manager = game_manager
    _node_registry = node_registry
    _get_active_connections = get_active_connections


async def collect_state():
    """
    Coleta o estado atual do jogo para broadcast.

    Returns:
        dict com global_score, nodes, events, active_connections
    """
    import db

    # Score global do GameManager (em memória)
    global_score = _game_manager.global_score if _game_manager else 0

    # Conexões RPC ativas
    active_conns = _get_active_connections() if _get_active_connections else 0

    # Nós com status e score
    nodes = []
    if _node_registry:
        all_nodes = await db.get_all_nodes()
        for n in all_nodes:
            nodes.append({
                "node_id": n.get("node_id"),
                "status": n.get("status"),
                "score": n.get("local_score", 0),
                "last_seen": n.get("last_seen")
            })

    # Últimos 20 eventos
    events = await db.get_recent_events(limit=20)

    return {
        "global_score": global_score,
        "nodes": nodes,
        "events": events,
        "active_connections": active_conns
    }


async def broadcast(message: str):
    """Envia mensagem para todos os clientes WebSocket conectados."""
    if not WS_CLIENTS:
        return

    for ws in WS_CLIENTS.copy():
        try:
            await ws.send(message)
        except ConnectionClosed:
            WS_CLIENTS.discard(ws)
        except Exception:
            WS_CLIENTS.discard(ws)


async def ws_handler(websocket):
    """Handler para conexões WebSocket."""
    WS_CLIENTS.add(websocket)
    try:
        # Envia estado inicial imediatamente
        state = await collect_state()
        await websocket.send(json.dumps(state))

        # Mantém conexão aberta, ignorando mensagens do cliente
        async for _ in websocket:
            pass
    except ConnectionClosed:
        pass
    except Exception:
        traceback.print_exc()
    finally:
        WS_CLIENTS.discard(websocket)


async def http_handler(reader, writer):
    """Handler HTTP manual para servir o dashboard."""
    try:
        request = await asyncio.wait_for(reader.read(4096), timeout=5.0)
        request_line = request.decode('utf-8', errors='ignore').split('\r\n')[0]

        # Parse simples: GET /path HTTP/1.x
        parts = request_line.split(' ')
        if len(parts) >= 2:
            method, path = parts[0], parts[1]
        else:
            method, path = 'GET', '/'

        # Roteamento básico
        if method == 'GET' and path in ('/', '/index.html'):
            body = INDEX_HTML.encode('utf-8')
            headers = (
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                f"Content-Length: {len(body)}\r\n"
                "Connection: close\r\n"
                "\r\n"
            )
            writer.write(headers.encode('utf-8') + body)
        else:
            # 404 para qualquer outro path (WebSocket é tratado separadamente)
            body = b"Not Found"
            headers = (
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                f"Content-Length: {len(body)}\r\n"
                "Connection: close\r\n"
                "\r\n"
            )
            writer.write(headers.encode('utf-8') + body)

        await writer.drain()
    except asyncio.TimeoutError:
        pass
    except Exception:
        pass
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


async def dashboard_broadcaster():
    """
    Loop de broadcast: envia estado a cada 0.5s para todos os clientes.
    Garante latência < 1s conforme critério de aceite.
    """
    while True:
        try:
            if WS_CLIENTS:
                state = await collect_state()
                await broadcast(json.dumps(state))
        except Exception:
            traceback.print_exc()

        await asyncio.sleep(0.5)


async def start_servers():
    """
    Inicia HTTP server e WebSocket server na porta 8080.
    Retorna tuple (http_server, ws_server) para controle.
    """
    # HTTP server para arquivos estáticos
    http_server = await asyncio.start_server(
        http_handler,
        "0.0.0.0",
        DASHBOARD_PORT
    )

    # WebSocket server no mesmo host, path /ws
    # Nota: websockets lib usa porta diferente ou process_request para routing
    # Aqui usamos porta 8081 para WS e fazemos proxy no frontend, ou
    # usamos a mesma porta com process_request customizado

    # Solução: WS na porta 8081, frontend conecta em :8081/ws
    ws_server = await ws_serve(
        ws_handler,
        "0.0.0.0",
        DASHBOARD_PORT + 1  # 8081
    )

    print(f"[Dashboard] HTTP server em http://0.0.0.0:{DASHBOARD_PORT}")
    print(f"[Dashboard] WebSocket server em ws://0.0.0.0:{DASHBOARD_PORT + 1}")

    return http_server, ws_server
