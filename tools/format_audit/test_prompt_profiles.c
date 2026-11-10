#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "../../types.h"
#include "../../alloc-inl.h"
#include "../../chat-llm.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    printf("[-] FAILED: %s (line %d): %s\n", __func__, __LINE__, msg); \
    return false; \
  } \
} while(0)

/* 1. Test binary protocol classification */
static bool test_protocol_classification(void) {
  g_tests_run++;
  TEST_ASSERT(is_binary_protocol("BACNET") == 1, "BACNET must be binary");
  TEST_ASSERT(is_binary_protocol("bacnet") == 1, "bacnet (lowercase) must be binary");
  TEST_ASSERT(is_binary_protocol("MODBUS") == 1, "MODBUS must be binary");
  TEST_ASSERT(is_binary_protocol("MODBUS_TCP") == 1, "MODBUS_TCP must be binary");
  TEST_ASSERT(is_binary_protocol("IEC104") == 1, "IEC104 must be binary");
  TEST_ASSERT(is_binary_protocol("IEC60870_5_104") == 1, "IEC60870_5_104 must be binary");
  TEST_ASSERT(is_binary_protocol("IEC61850") == 1, "IEC61850 must be binary");
  TEST_ASSERT(is_binary_protocol("MMS") == 1, "MMS must be binary");
  TEST_ASSERT(is_binary_protocol("OPCUA") == 1, "OPCUA must be binary");
  TEST_ASSERT(is_binary_protocol("OPC_UA") == 1, "OPC_UA must be binary");
  TEST_ASSERT(is_binary_protocol("ETHERNET_IP") == 1, "ETHERNET_IP must be binary");
  TEST_ASSERT(is_binary_protocol("ENIP") == 1, "ENIP must be binary");

  /* Text protocols */
  TEST_ASSERT(is_binary_protocol("HTTP") == 0, "HTTP must NOT be binary");
  TEST_ASSERT(is_binary_protocol("RTSP") == 0, "RTSP must NOT be binary");
  TEST_ASSERT(is_binary_protocol("FTP") == 0, "FTP must NOT be binary");
  TEST_ASSERT(is_binary_protocol("SMTP") == 0, "SMTP must NOT be binary");
  TEST_ASSERT(is_binary_protocol("SIP") == 0, "SIP must NOT be binary");
  TEST_ASSERT(is_binary_protocol(NULL) == 0, "NULL must NOT be binary");

  g_tests_passed++;
  return true;
}

/* 2. Test message classification for industrial protocols */
static bool test_message_classification(void) {
  g_tests_run++;

  /* BACnet Who-Is */
  uint8_t bacnet_whois[] = { 0x81, 0x0a, 0x00, 0x08, 0x01, 0x00, 0x10, 0x08 };
  char *t1 = classify_protocol_message_type("BACNET", bacnet_whois, sizeof(bacnet_whois));
  TEST_ASSERT(t1 != NULL && !strcmp(t1, "WHO_IS"), "BACnet Who-Is must be classified as WHO_IS");
  ck_free(t1);

  /* BACnet ReadProperty */
  uint8_t bacnet_rp[] = { 0x81, 0x0a, 0x00, 0x11, 0x01, 0x00, 0x00, 0x01, 0x0c, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x19, 0x55 };
  char *t2 = classify_protocol_message_type("BACNET", bacnet_rp, sizeof(bacnet_rp));
  TEST_ASSERT(t2 != NULL && !strcmp(t2, "READ_PROPERTY"), "BACnet ReadProperty must be classified as READ_PROPERTY");
  ck_free(t2);

  /* Modbus ReadHoldingRegisters (FC 0x03) */
  uint8_t modbus_fc3[] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x0a };
  char *t3 = classify_protocol_message_type("MODBUS", modbus_fc3, sizeof(modbus_fc3));
  TEST_ASSERT(t3 != NULL && !strcmp(t3, "READ_HOLDING_REGISTERS"), "Modbus FC3 must be READ_HOLDING_REGISTERS");
  ck_free(t3);

  /* Modbus WriteSingleRegister (FC 0x06) */
  uint8_t modbus_fc6[] = { 0x00, 0x02, 0x00, 0x00, 0x00, 0x06, 0x01, 0x06, 0x00, 0x01, 0x00, 0x03 };
  char *t4 = classify_protocol_message_type("MODBUS", modbus_fc6, sizeof(modbus_fc6));
  TEST_ASSERT(t4 != NULL && !strcmp(t4, "WRITE_SINGLE_REGISTER"), "Modbus FC6 must be WRITE_SINGLE_REGISTER");
  ck_free(t4);

  /* IEC104 STARTDT act */
  uint8_t iec_startdt[] = { 0x68, 0x04, 0x07, 0x00, 0x00, 0x00 };
  char *t5 = classify_protocol_message_type("IEC104", iec_startdt, sizeof(iec_startdt));
  TEST_ASSERT(t5 != NULL && !strcmp(t5, "STARTDT_ACT"), "IEC104 STARTDT act must be STARTDT_ACT");
  ck_free(t5);

  /* IEC104 TESTFR act */
  uint8_t iec_testfr[] = { 0x68, 0x04, 0x43, 0x00, 0x00, 0x00 };
  char *t6 = classify_protocol_message_type("IEC104", iec_testfr, sizeof(iec_testfr));
  TEST_ASSERT(t6 != NULL && !strcmp(t6, "TESTFR_ACT"), "IEC104 TESTFR act must be TESTFR_ACT");
  ck_free(t6);

  /* IEC104 Interrogation (ASDU 100) */
  uint8_t iec_interrog[] = { 0x68, 0x0e, 0x00, 0x00, 0x00, 0x00, 100, 0x01, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x14 };
  char *t7 = classify_protocol_message_type("IEC104", iec_interrog, sizeof(iec_interrog));
  TEST_ASSERT(t7 != NULL && !strcmp(t7, "INTERROGATION"), "IEC104 Interrogation must be INTERROGATION");
  ck_free(t7);

  /* OPC UA Hello */
  uint8_t opc_hel[] = { 'H', 'E', 'L', 'F', 0x20, 0x00, 0x00, 0x00 };
  char *t8 = classify_protocol_message_type("OPCUA", opc_hel, sizeof(opc_hel));
  TEST_ASSERT(t8 != NULL && !strcmp(t8, "HELLO"), "OPC UA HEL must be HELLO");
  ck_free(t8);

  /* OPC UA OpenSecureChannel */
  uint8_t opc_opn[] = { 'O', 'P', 'N', 'F', 0x40, 0x00, 0x00, 0x00 };
  char *t9 = classify_protocol_message_type("OPCUA", opc_opn, sizeof(opc_opn));
  TEST_ASSERT(t9 != NULL && !strcmp(t9, "OPEN_SECURE_CHANNEL"), "OPC UA OPN must be OPEN_SECURE_CHANNEL");
  ck_free(t9);

  /* EtherNet/IP ListIdentity (0x0063) */
  uint8_t enip_listid[] = { 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  char *t10 = classify_protocol_message_type("ETHERNET_IP", enip_listid, sizeof(enip_listid));
  TEST_ASSERT(t10 != NULL && !strcmp(t10, "LIST_IDENTITY"), "EtherNet/IP ListIdentity must be LIST_IDENTITY");
  ck_free(t10);

  /* EtherNet/IP RegisterSession (0x0065) */
  uint8_t enip_reg[] = { 0x65, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };
  char *t11 = classify_protocol_message_type("ENIP", enip_reg, sizeof(enip_reg));
  TEST_ASSERT(t11 != NULL && !strcmp(t11, "REGISTER_SESSION"), "EtherNet/IP RegisterSession must be REGISTER_SESSION");
  ck_free(t11);

  g_tests_passed++;
  return true;
}

/* 3. Test Hex encoding / decoding and markdown payload extraction */
static bool test_hex_utilities(void) {
  g_tests_run++;

  uint8_t raw[] = { 0x81, 0x0a, 0x00, 0x08, 0x01, 0x00, 0x10, 0x08 };
  char *hex = bytes_to_hex_str(raw, sizeof(raw), 0);
  TEST_ASSERT(hex != NULL && !strcmp(hex, "810a000801001008"), "Continuous hex string match");

  size_t decoded_len = 0;
  unsigned char *dec = hex_str_to_bytes(hex, &decoded_len);
  TEST_ASSERT(dec != NULL && decoded_len == sizeof(raw), "Decoded length match");
  TEST_ASSERT(memcmp(raw, dec, sizeof(raw)) == 0, "Decoded payload match");
  ck_free(hex);
  ck_free(dec);

  /* Spaced hex */
  char *spaced_hex = bytes_to_hex_str(raw, sizeof(raw), 1);
  TEST_ASSERT(spaced_hex != NULL && !strcmp(spaced_hex, "81 0a 00 08 01 00 10 08"), "Spaced hex string match");
  unsigned char *dec_spaced = hex_str_to_bytes(spaced_hex, &decoded_len);
  TEST_ASSERT(dec_spaced != NULL && decoded_len == sizeof(raw), "Decoded spaced length match");
  TEST_ASSERT(memcmp(raw, dec_spaced, sizeof(raw)) == 0, "Decoded spaced payload match");
  ck_free(spaced_hex);
  ck_free(dec_spaced);

  /* Fenced markdown hex payload extraction */
  const char *md = "Here is the next client request:\n```hex\n810a000801001008\n```\nHope it works!";
  size_t extracted_len = 0;
  char *extracted = extract_hex_payload(md, strlen(md), &extracted_len);
  TEST_ASSERT(extracted != NULL && extracted_len == sizeof(raw), "Extracted fenced hex length match");
  TEST_ASSERT(memcmp(raw, extracted, sizeof(raw)) == 0, "Extracted fenced hex payload match");
  ck_free(extracted);

  g_tests_passed++;
  return true;
}

/* 4. Test Prompt Generation tailoring for industrial protocols */
static bool test_prompt_generation(void) {
  g_tests_run++;

  /* BACnet template prompt */
  char *final_msg = NULL;
  char *p_bacnet = construct_prompt_for_templates("BACNET", &final_msg);
  TEST_ASSERT(p_bacnet != NULL, "BACnet prompt must not be NULL");
  TEST_ASSERT(strstr(p_bacnet, "BVLC") != NULL, "BACnet prompt must mention BVLC");
  TEST_ASSERT(strstr(p_bacnet, "WHO_IS") != NULL, "BACnet prompt must include WHO_IS template");
  TEST_ASSERT(strstr(p_bacnet, "RTSP") == NULL, "BACnet prompt must NOT mention RTSP");
  free(p_bacnet);
  free(final_msg);

  /* Modbus template prompt */
  final_msg = NULL;
  char *p_modbus = construct_prompt_for_templates("MODBUS", &final_msg);
  TEST_ASSERT(p_modbus != NULL, "Modbus prompt must not be NULL");
  TEST_ASSERT(strstr(p_modbus, "MBAP") != NULL, "Modbus prompt must mention MBAP");
  TEST_ASSERT(strstr(p_modbus, "READ_HOLDING_REGISTERS") != NULL, "Modbus prompt must include READ_HOLDING_REGISTERS");
  TEST_ASSERT(strstr(p_modbus, "RTSP") == NULL, "Modbus prompt must NOT mention RTSP");
  free(p_modbus);
  free(final_msg);

  /* IEC104 template prompt */
  final_msg = NULL;
  char *p_iec = construct_prompt_for_templates("IEC104", &final_msg);
  TEST_ASSERT(p_iec != NULL, "IEC104 prompt must not be NULL");
  TEST_ASSERT(strstr(p_iec, "STARTDT_ACT") != NULL, "IEC104 prompt must include STARTDT_ACT");
  TEST_ASSERT(strstr(p_iec, "68") != NULL, "IEC104 prompt must mention start byte 68");
  free(p_iec);
  free(final_msg);

  /* HTTP template prompt (must preserve standard text examples) */
  final_msg = NULL;
  char *p_http = construct_prompt_for_templates("HTTP", &final_msg);
  TEST_ASSERT(p_http != NULL, "HTTP prompt must not be NULL");
  TEST_ASSERT(strstr(p_http, "GET <<VALUE>>") != NULL, "HTTP prompt must mention GET");
  TEST_ASSERT(strstr(p_http, "DESCRIBE") != NULL, "HTTP prompt must mention DESCRIBE example");
  free(p_http);
  free(final_msg);

  /* Stall prompt for binary protocol */
  char *p_stall_bacnet = construct_prompt_stall("BACNET", "Req1\n", "Hist1\n");
  TEST_ASSERT(p_stall_bacnet != NULL, "Stall prompt must not be NULL");
  TEST_ASSERT(strstr(p_stall_bacnet, "hexadecimal") != NULL, "Stall prompt for BACnet must specify hexadecimal");
  free(p_stall_bacnet);

  /* Protocol message types */
  char *types_bacnet = construct_prompt_for_protocol_message_types("BACNET");
  TEST_ASSERT(types_bacnet != NULL, "Message types prompt must not be NULL");
  TEST_ASSERT(strstr(types_bacnet, "WHO_IS") != NULL, "BACnet message types must hint WHO_IS");
  free(types_bacnet);

  char *types_modbus = construct_prompt_for_protocol_message_types("MODBUS");
  TEST_ASSERT(types_modbus != NULL, "Modbus message types prompt must not be NULL");
  TEST_ASSERT(strstr(types_modbus, "READ_HOLDING_REGISTERS") != NULL, "Modbus message types must hint READ_HOLDING_REGISTERS");
  free(types_modbus);

  g_tests_passed++;
  return true;
}

int main(void) {
  printf("[*] Running Industrial Protocol LLM Prompt Adaptation Unit Tests...\n");

  bool ok = true;
  ok &= test_protocol_classification();
  ok &= test_message_classification();
  ok &= test_hex_utilities();
  ok &= test_prompt_generation();

  printf("\n========================================================\n");
  if (ok && g_tests_run == g_tests_passed) {
    printf("[+] Prompt Adaptation Tests: ALL %d / %d tests passed (100.0%%)\n", g_tests_passed, g_tests_run);
    printf("========================================================\n");
    return 0;
  } else {
    printf("[-] Prompt Adaptation Tests: %d / %d tests passed\n", g_tests_passed, g_tests_run);
    printf("========================================================\n");
    return 1;
  }
}
