# Plano de Testes - rpc_client.h

## Teste Manual 1: Inicialização

```c
void test_init() {
    RpcSimpleResult r = rpc_init();
    assert(r.success == true);
    printf("✅ rpc_init OK\n");
}
```

**Esperado:** Estado interno do cliente RPC inicializado sem erros.

---

## Teste Manual 2: Add Clicks Online

**Pré-condição:** Servidor rodando em 192.168.0.10:8765

```c
void test_add_clicks_online() {
    rpc_register_node(0);
    RpcClickResult r = rpc_add_clicks(10, 42);
    
    assert(r.success == true);
    assert(r.global_score > 0);
    assert(r.lamport_ts >= 42);
    printf("✅ add_clicks online OK\n");
}
```

---

## Teste Manual 3: Add Clicks Offline (Enfileiramento)

**Pré-condição:** Servidor offline

```c
void test_add_clicks_offline() {
    RpcClickResult r = rpc_add_clicks(5, 10);
    
    assert(r.success == false);
    assert(r.error_code == RPC_DISCONNECTED);
    printf("✅ Enfileiramento offline OK\n");
    
    // Religar servidor e aguardar 2s
    sleep(3);
    // Verificar que fila foi drenada (via logs ou get_scores)
}
```

---

## Teste End-to-End

1. Flashar firmware em 3 placas
2. Ligar servidor
3. Pressionar botão A em cada placa → verificar scores no dashboard
4. Desligar servidor, pressionar mais botões
5. Religar servidor, aguardar 2s
6. Verificar que cliques offline foram sincronizados
