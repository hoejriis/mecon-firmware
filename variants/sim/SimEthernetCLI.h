#pragma once

// Simulated board: stand-in for the RAK4631 Ethernet TCP console (src/helpers/nrf52/EthernetCLI.h), which needs the
// RAK13800 chip. Same interface and the same line handling as the real header, over a loopback TCP listener whose
// port is written to "<port file>.eth". The repeater and room server mains include it under SIM_ETHERNET_CONSOLE in
// place of the real header, so the code between the socket and the command handler is the firmware's own: the login
// state, the gate, the dispatch. Only the W5100S driver is not.

#ifdef ETHERNET_ENABLED

#include <Arduino.h>
#include <helpers/cli/NetConsole.h>
#include <helpers/LineAssembler.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include "sim_config.h"

static Cli::NetConsole ethernet_console;
static LineAssembler ethernet_line;
static int sim_eth_listen = -1, sim_eth_client = -1;

static void ethernet_start_task() {
  sim_eth_listen = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(sim_eth_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
  if (bind(sim_eth_listen, (sockaddr*)&a, sizeof a) != 0 || listen(sim_eth_listen, 1) != 0) { perror("eth bind"); exit(2); }
  socklen_t l = sizeof a; getsockname(sim_eth_listen, (sockaddr*)&a, &l);
  fcntl(sim_eth_listen, F_SETFL, O_NONBLOCK);
  if (!g_sim.port_file.empty()) {
    std::string path = g_sim.port_file + ".eth", tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (f) { fprintf(f, "%d\n", ntohs(a.sin_port)); fclose(f); rename(tmp.c_str(), path.c_str()); }
  }
}

static void sim_eth_send(const char* s) {
  if (sim_eth_client < 0) return;
  size_t n = strlen(s), off = 0;
  while (off < n) {
    ssize_t w = send(sim_eth_client, s + off, n - off, MSG_NOSIGNAL);
    if (w > 0) off += w;
    else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) usleep(100);
    else { close(sim_eth_client); sim_eth_client = -1; break; }
  }
}

static bool ethernet_handle_command(const char* command, char* reply) {
  if (strcmp(command, "eth.status") == 0) { strcpy(reply, "ETH: sim"); return true; }
  return false;
}

static void ethernet_loop_maintain() {
  if (sim_eth_listen < 0) return;
  int c = accept(sim_eth_listen, nullptr, nullptr);
  if (c >= 0) {
    if (sim_eth_client >= 0) close(sim_eth_client);   // a new connection replaces the old one, as on the board
    int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fcntl(c, F_SETFL, O_NONBLOCK);
    sim_eth_client = c;
    ethernet_line.reset();   // a partial line of the previous client is not the start of this one
    ethernet_console.endSession();
    sim_eth_send(ETHERNET_CLI_BANNER "\n");
  }
}

// The same line assembly as the real header (helpers/LineAssembler.h).
static bool ethernet_read_line(char* ethernet_command, size_t buf_size) {
  if (sim_eth_client < 0) { ethernet_console.endSession(); return false; }
  for (;;) {
    char c;
    ssize_t n = recv(sim_eth_client, &c, 1, 0);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { close(sim_eth_client); sim_eth_client = -1; ethernet_console.endSession(); return false; }
    if (n < 0) return false;
    switch (ethernet_line.feed(ethernet_command, buf_size, c)) {
      case LineAssembler::LINE: sim_eth_send("\n"); return true;
      case LineAssembler::TOO_LONG: sim_eth_send("  -> ERR: line too long\n"); break;
      default: break;
    }
  }
}

static void ethernet_send_reply(const char* reply) {
  if (reply[0]) { sim_eth_send("  -> "); sim_eth_send(reply); sim_eth_send("\n"); }
}

#endif  // ETHERNET_ENABLED
