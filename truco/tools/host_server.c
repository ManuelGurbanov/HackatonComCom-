#include <time.h>
// Servidor de prueba para PC: usa game.c real + el protocolo de net.c.
// El "jugador 0" (servidor) es un bot. Sirve para probar sim_client.py sin placas.
// gcc -I../main -Istub host_server.c ../main/game.c -o /tmp/host_server
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "game.h"

uint32_t esp_random(void) { return (uint32_t)rand(); }

static int bot(game_t *g)
{
    view_t v;
    game_view(g, 0, &v);
    if (!v.turn) return 0;
    if (v.pending) return game_action(g, 0, rand() % 4 ? ACT_QUIERO : ACT_NOQUIERO, 0);
    if (v.can_envido && rand() % 4 == 0) return game_action(g, 0, ACT_ENVIDO, 0);
    for (int i = 0; i < 3; i++) if (v.hand[i] != NO_CARD) return game_action(g, 0, ACT_PLAY, i);
    return 0;
}

static void send_view(int c, game_t *g)
{
    view_t v;
    game_view(g, 1, &v);
    uint8_t buf[1 + sizeof v] = {1};
    memcpy(buf + 1, &v, sizeof v);
    send(c, buf, sizeof buf, 0);
}

int main(void)
{
    srand(time(NULL) ^ getpid());
    printf("sizeof(view_t)=%zu\n", sizeof(view_t));
    int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(3333), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    bind(ls, (void *)&a, sizeof a);
    listen(ls, 1);
    for (;;) {
        int c = accept(ls, NULL, NULL);
        game_t g;
        game_new(&g);
        send_view(c, &g);
        fprintf(stderr, "cliente conectado\n");
        for (;;) {
            uint8_t m[3];
            if (g.phase == PH_HAND_END) { sleep(1); game_next_hand(&g); send_view(c, &g); }
            while (bot(&g)) send_view(c, &g);
            if (g.phase == PH_HAND_END) { send_view(c, &g); continue; }
            size_t got = 0;
            while (got < 3) { ssize_t r = recv(c, m + got, 3 - got, 0); if (r <= 0) goto closed; got += r; }
            if (m[0] == 2) game_action(&g, 1, m[1], m[2]);
            send_view(c, &g);
        }
closed:
        close(c);
        fprintf(stderr, "cliente desconectado\n");
    }
}
