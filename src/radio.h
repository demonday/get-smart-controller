
#ifdef __cplusplus
extern "C" {
#endif

int radio_init();
int radio_tx(uint8_t* msg, uint8_t len);
int radio_tx_repeat(uint8_t* msg, uint8_t len, uint8_t count);

#ifdef __cplusplus
}
#endif