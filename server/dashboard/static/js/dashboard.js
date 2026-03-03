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
    asyncioTasks: document.getElementById('asyncio-tasks'),
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
    return new Date(ts * 1000).toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' });
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

    el.activeConnections.textContent = data.active_connections ?? 0;
    if (el.asyncioTasks && data.asyncio_tasks != null) {
        el.asyncioTasks.textContent = data.asyncio_tasks;
    }

    renderNodes(data.nodes);
    updateCounts(data.nodes);
    renderEvents(data.events);
}

// ===== WebSocket =====
function connect() {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    // Porta do WS definida no config.py como DASHBOARD_WS_PORT (8081)
    // No legado era port+1. Mantemos a lógica ou fixamos.
    // Para flexibilidade, tentamos pegar do config via injeção ou assumimos padrão.
    const ws = new WebSocket(`${protocol}//${location.hostname}:8081`);

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
            const msg = JSON.parse(event.data);
            // Suporta envelope tipado {type, payload} (novo formato event-driven)
            // e payload plano (broadcast_loop safety-net / full_state)
            const data = (msg.type && msg.payload !== undefined) ? msg.payload : msg;
            updateDashboard(data);
        } catch (e) {
            console.error('Parse error:', e);
        }
    };
}

// ===== Init =====
connect();
setInterval(rotateMessage, 8000);
