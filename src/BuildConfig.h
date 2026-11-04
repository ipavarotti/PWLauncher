#pragma once
#include <string>
#include <cstdint>

// ===========================================================================
//  CONFIGURACAO EMBUTIDA NO EXECUTAVEL
//  Edite os valores abaixo e recompile. Eles ficam "dentro" do launcher e
//  sao usados quando os arquivos externos nao existem (ou para forcar).
// ===========================================================================

namespace el {

// Endereco do servidor de atualizacao embutido.
// Use vazio (L"") para desativar e depender so dos arquivos externos.
constexpr const wchar_t* kEmbeddedUpdateServer = L"http://172.21.119.132/CPW/";

// Numero de serie (pid) esperado. Usado se patcher/server/pid.ini nao existir.
constexpr int kEmbeddedPid = 101;

// Se true, um pid do servidor diferente do local bloqueia a atualizacao.
// Se false (padrao), apenas registra um aviso e continua.
constexpr bool kStrictPidCheck = false;

// Se true, valida a assinatura RSA das listas (files.md5 / v-N.inc) usando a
// chave publica de src/Signature.cpp. Deixe false se o seu servidor assina com
// outra chave (a integridade continua garantida pelo MD5 de cada arquivo).
constexpr bool kVerifySignature = true;

// Se true, arquivos que retornam 404 no servidor sao registrados no log e
// ignorados (o launcher continua). Se false, o primeiro 404 aborta a atualizacao.
constexpr bool kSkipMissingFiles = true;

// Se true, o endereco embutido acima tem prioridade sobre o updateserver.txt
// (o arquivo externo passa a ser ignorado, exceto por /updateserver: na linha
// de comando). Se false, o endereco embutido e apenas o fallback.
constexpr bool kEmbeddedServerOverride = false;

// ---------------------------------------------------------------------------
//  CHAVES DO FORMATO .pck  (perfil do client)
//     Key1 (mask)      = 0xA33629E2  (low dword; o dwEntryOffset usa o
//                                      MASKDWORD completo 0xFFFFFFFFA33629E2)
//     XOR (checkMask)  = 0x51E5F551
//  O dwEntryOffset (DWORD64) e XORed com (0xFFFFFFFF00000000 | mask).
// ---------------------------------------------------------------------------
constexpr uint32_t kPckGuard0    = 0xfdfdfeee;  // guardByte0 do cabecalho
constexpr uint32_t kPckGuard1    = 0xf00dbeef;  // guardByte1 do cabecalho
constexpr uint32_t kPckMask      = 0xA33629E2;  // Key1 (low dword do MASKDWORD)
constexpr uint32_t kPckCheckMask = 0x51E5F551;  // AFPCK_CHECKMASK

}
