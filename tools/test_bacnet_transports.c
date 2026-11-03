#include "bacnet/apdu.h"
#include "bacnet/bacapp.h"
#include "bacnet/bacaddr.h"
#include "bacnet/bacdcode.h"
#include "bacnet/iam.h"
#include "bacnet/whois.h"
#include "bacnet/cov.h"
#include "bacnet/rp.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/basic/service/h_cov.h"
#include "bacnet/basic/service/h_whois.h"
#include "bacnet/basic/service/h_rp.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/basic/services.h"
#include "bacnet/datalink/datalink.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *link;
    BACNET_ADDRESS dest;
    unsigned length;
    uint8_t bytes[MAX_PDU];
} packet_t;
static packet_t packets[256];
static unsigned packet_count;
static bool changed;
static float present_value = 12.5f;
static bool fail_send;

/* Deliberately test-only object/identity: no compiled gateway mappings are altered. */
uint32_t Device_Object_Instance_Number(void) { return 1234; }
uint16_t Device_Vendor_Identifier(void) { return 260; }
const char *Device_Model_Name(void) { return "Test"; }
const char *Device_Serial_Number(void) { return "Test"; }
uint32_t Network_Port_Index_To_Instance(unsigned index) { (void)index; return 0; }
bool Device_Valid_Object_Id(BACNET_OBJECT_TYPE type, uint32_t instance)
{ return type == OBJECT_ANALOG_VALUE && instance == 42; }
bool Device_Value_List_Supported(BACNET_OBJECT_TYPE type)
{ return type == OBJECT_ANALOG_VALUE; }
bool Device_COV(BACNET_OBJECT_TYPE type, uint32_t instance)
{ assert(Device_Valid_Object_Id(type, instance)); return changed; }
void Device_COV_Clear(BACNET_OBJECT_TYPE type, uint32_t instance)
{ assert(Device_Valid_Object_Id(type, instance)); changed = false; }
bool Device_Encode_Value_List(BACNET_OBJECT_TYPE type, uint32_t instance,
    BACNET_PROPERTY_VALUE *list)
{
    assert(Device_Valid_Object_Id(type, instance));
    list->propertyIdentifier = PROP_PRESENT_VALUE;
    list->propertyArrayIndex = BACNET_ARRAY_ALL;
    list->value.tag = BACNET_APPLICATION_TAG_REAL;
    list->value.type.Real = present_value;
    list->next = NULL;
    return true;
}
int Device_Read_Property(BACNET_READ_PROPERTY_DATA *rp)
{
    if (rp->object_type == OBJECT_DEVICE && rp->object_instance == 1234 &&
        rp->object_property == PROP_OBJECT_IDENTIFIER) {
        return encode_application_object_id(rp->application_data, OBJECT_DEVICE, 1234);
    }
    if (Device_Valid_Object_Id(rp->object_type, rp->object_instance) &&
        rp->object_property == PROP_PRESENT_VALUE) {
        return encode_application_real(rp->application_data, present_value);
    }
    rp->error_class = ERROR_CLASS_OBJECT;
    rp->error_code = ERROR_CODE_UNKNOWN_OBJECT;
    return BACNET_STATUS_ERROR;
}

static void my_address(BACNET_ADDRESS *address, bool ip)
{
    memset(address, 0, sizeof(*address));
    address->mac_len = ip ? 6 : 1;
    address->mac[0] = ip ? 192 : 34;
}
void bip_get_my_address(BACNET_ADDRESS *a) { my_address(a, true); }
void dlmstp_get_my_address(BACNET_ADDRESS *a) { my_address(a, false); }
void bip_get_broadcast_address(BACNET_ADDRESS *a)
{ my_address(a, true); memset(a->mac, 255, 6); a->net = BACNET_BROADCAST_NETWORK; }
void dlmstp_get_broadcast_address(BACNET_ADDRESS *a)
{ my_address(a, false); a->mac[0] = 255; a->net = BACNET_BROADCAST_NETWORK; }
bool bip_init(char *interface) { (void)interface; return true; }
bool dlmstp_init(char *interface) { (void)interface; return true; }
void bip_cleanup(void) {}
void dlmstp_cleanup(void) {}
void bvlc_maintenance_timer(uint16_t seconds) { (void)seconds; }
uint16_t bip_receive(BACNET_ADDRESS *src, uint8_t *pdu, uint16_t size, unsigned timeout)
{ (void)src; (void)pdu; (void)size; (void)timeout; return 0; }
uint16_t dlmstp_receive(BACNET_ADDRESS *src, uint8_t *pdu, uint16_t size, unsigned timeout)
{ (void)src; (void)pdu; (void)size; (void)timeout; return 0; }

static int capture(const char *link, BACNET_ADDRESS *dest, uint8_t *pdu, unsigned length)
{
    assert(packet_count < 256 && length <= MAX_PDU);
    packet_t *packet = &packets[packet_count++];
    packet->link = link;
    packet->dest = *dest;
    packet->length = length;
    memcpy(packet->bytes, pdu, length);
    return fail_send ? 0 : (int)length;
}
int bip_send_pdu(BACNET_ADDRESS *d, BACNET_NPDU_DATA *n, uint8_t *p, unsigned size)
{ (void)n; return capture("bip", d, p, size); }
int dlmstp_send_pdu(BACNET_ADDRESS *d, BACNET_NPDU_DATA *n, uint8_t *p, unsigned size)
{ (void)n; return capture("mstp", d, p, size); }

static uint8_t *apdu(packet_t *packet, unsigned *length)
{
    BACNET_ADDRESS src = {0}, dest = {0};
    BACNET_NPDU_DATA npdu = {0};
    int offset = bacnet_npdu_decode(packet->bytes, packet->length, &dest, &src, &npdu);
    assert(offset > 0 && (unsigned)offset < packet->length);
    *length = packet->length - (unsigned)offset;
    return packet->bytes + offset;
}

static void receive(const char *link, BACNET_ADDRESS *src, uint8_t *data, unsigned length)
{
    /* Same ingress-link selection used under the application datalink lock. */
    datalink_set(link);
    apdu_handler(src, data, (uint16_t)length);
    assert(strcmp(datalink_name(), link) == 0);
    datalink_set("bip");
}

static void scan(const char *background_link)
{
    datalink_set(background_link);
    for (unsigned i = 0; i < 1024; ++i) {
        if (handler_cov_fsm()) {
            assert(strcmp(datalink_name(), background_link) == 0);
            return;
        }
    }
    assert(false);
}

static void subscribe(const char *link, BACNET_ADDRESS *src, unsigned process,
    bool confirmed, unsigned lifetime, bool cancel, bool property)
{
    uint8_t data[MAX_APDU];
    BACNET_SUBSCRIBE_COV_DATA cov = {
        .subscriberProcessIdentifier = process,
        .monitoredObjectIdentifier = {.type = OBJECT_ANALOG_VALUE, .instance = 42},
        .issueConfirmedNotifications = confirmed, .lifetime = lifetime,
        .cancellationRequest = cancel,
        .monitoredProperty = {
            .property_identifier = PROP_PRESENT_VALUE, .property_array_index = BACNET_ARRAY_ALL
        },
    };
    int length = property ?
        cov_subscribe_property_encode_apdu(data, sizeof(data), 9, &cov) :
        cov_subscribe_encode_apdu(data, sizeof(data), 9, &cov);
    assert(length > 0);
    unsigned before = packet_count, apdu_length;
    receive(link, src, data, (unsigned)length);
    assert(packet_count == before + 1);
    packet_t *reply = &packets[before];
    uint8_t *response = apdu(reply, &apdu_length);
    assert(strcmp(reply->link, link) == 0 && bacnet_address_same(&reply->dest, src));
    assert(apdu_length == 3 && response[0] == PDU_TYPE_SIMPLE_ACK && response[1] == 9);
}

static uint8_t check_notification(unsigned index, const char *link,
    BACNET_ADDRESS *dest, unsigned process, bool confirmed, float expected_value)
{
    packet_t *packet = &packets[index];
    unsigned length;
    uint8_t *data = apdu(packet, &length);
    assert(strcmp(packet->link, link) == 0 && bacnet_address_same(&packet->dest, dest));
    BACNET_PROPERTY_VALUE values[2] = {0};
    cov_property_value_list_link(values, 2);
    BACNET_COV_DATA cov = {.listOfValues = values};
    uint8_t invoke_id = 0;
    unsigned offset = confirmed ? 4 : 2;
    assert(length > offset);
    if (confirmed) {
        assert(data[0] == PDU_TYPE_CONFIRMED_SERVICE_REQUEST &&
            data[3] == SERVICE_CONFIRMED_COV_NOTIFICATION);
        invoke_id = data[2];
    } else {
        assert(data[0] == PDU_TYPE_UNCONFIRMED_SERVICE_REQUEST &&
            data[1] == SERVICE_UNCONFIRMED_COV_NOTIFICATION);
    }
    int result = cov_notify_decode_service_request(data + offset, length - offset, &cov);
    assert(result > 0 && cov.subscriberProcessIdentifier == process &&
        cov.initiatingDeviceIdentifier == 1234 &&
        cov.monitoredObjectIdentifier.instance == 42 &&
        values[0].value.tag == BACNET_APPLICATION_TAG_REAL &&
        values[0].value.type.Real == expected_value);
    return invoke_id;
}

static void ack(const char *link, BACNET_ADDRESS *src, uint8_t id)
{
    uint8_t data[3] = {PDU_TYPE_SIMPLE_ACK, id, SERVICE_CONFIRMED_COV_NOTIFICATION};
    receive(link, src, data, sizeof(data));
    assert(tsm_invoke_id_free(id));
}

static void discovery_and_reads(const char *link, BACNET_ADDRESS *src)
{
    uint8_t data[MAX_APDU];
    unsigned before = packet_count, length;
    int size = whois_encode_apdu(data, -1, -1);
    receive(link, src, data, (unsigned)size);
    assert(packet_count == before + 1 && strcmp(packets[before].link, link) == 0);
    uint8_t *response = apdu(&packets[before], &length);
    assert(response[0] == PDU_TYPE_UNCONFIRMED_SERVICE_REQUEST &&
        response[1] == SERVICE_UNCONFIRMED_I_AM);
    uint32_t device; unsigned max_apdu; int segmentation; uint16_t vendor;
    assert(iam_decode_service_request(response + 2, &device, &max_apdu, &segmentation, &vendor) > 0);
    assert(device == 1234 && vendor == 260);
    before = packet_count;
    size = whois_encode_apdu(data, 5000, 6000);
    receive(link, src, data, (unsigned)size);
    assert(packet_count == before);
    size = whois_encode_apdu(data, 1234, 1234);
    receive(link, src, data, (unsigned)size);
    assert(packet_count == before + 1);
    for (unsigned i = 0; i < 3; ++i) {
        BACNET_READ_PROPERTY_DATA request = {
            .object_type = i == 0 ? OBJECT_DEVICE : OBJECT_ANALOG_VALUE,
            .object_instance = i == 0 ? BACNET_MAX_INSTANCE : i == 1 ? 42 : 999,
            .object_property = i == 0 ? PROP_OBJECT_IDENTIFIER : PROP_PRESENT_VALUE,
            .array_index = BACNET_ARRAY_ALL,
        };
        size = rp_encode_apdu(data, 17, &request);
        before = packet_count;
        receive(link, src, data, (unsigned)size);
        assert(packet_count == before + 1 && strcmp(packets[before].link, link) == 0 &&
            bacnet_address_same(&packets[before].dest, src));
        response = apdu(&packets[before], &length);
        assert(response[1] == 17 && response[2] == SERVICE_CONFIRMED_READ_PROPERTY);
        if (i == 2) {
            assert(response[0] == PDU_TYPE_ERROR);
        } else {
            assert(response[0] == PDU_TYPE_COMPLEX_ACK);
            BACNET_READ_PROPERTY_DATA reply = {0};
            assert(rp_ack_decode_service_request(response + 3, (int)length - 3, &reply) > 0);
            BACNET_APPLICATION_DATA_VALUE value = {0};
            assert(bacapp_decode_application_data(reply.application_data, reply.application_data_len, &value) > 0);
            if (i == 0) assert(value.tag == BACNET_APPLICATION_TAG_OBJECT_ID && value.type.Object_Id.instance == 1234);
            else assert(value.tag == BACNET_APPLICATION_TAG_REAL && value.type.Real == present_value);
        }
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    bool dual = strcmp(argv[1], "dual") == 0;
    const char *first = strcmp(argv[1], "mstp") == 0 ? "mstp" : "bip";
    BACNET_ADDRESS ip = {.mac_len = 6, .mac = {192, 0, 2, 1, 0xBA, 0xC0}};
    BACNET_ADDRESS mstp = {.mac_len = 1, .mac = {12}};
    BACNET_ADDRESS *one = strcmp(first, "bip") == 0 ? &ip : &mstp;
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV, handler_cov_subscribe);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_SUBSCRIBE_COV_PROPERTY, handler_cov_subscribe_property);
    handler_cov_init();
    discovery_and_reads(first, one);
    if (dual) discovery_and_reads("mstp", &mstp);
    /* Same object and process ID must receive initial and changed COVs on both links. */
    subscribe(first, one, 100, false, 30, false, false);
    if (dual) subscribe("mstp", &mstp, 100, false, 30, false, true);
    unsigned before = packet_count;
    scan("mstp");
    assert(packet_count == before + (dual ? 2 : 1));
    check_notification(before, first, one, 100, false, present_value);
    if (dual) check_notification(before + 1, "mstp", &mstp, 100, false, present_value);
    changed = true; present_value = 19;
    before = packet_count;
    scan("bip");
    assert(packet_count == before + (dual ? 2 : 1) && !changed);
    check_notification(before, first, one, 100, false, 19);
    if (dual) check_notification(before + 1, "mstp", &mstp, 100, false, 19);
    before = packet_count;
    scan(first);
    assert(packet_count == before);
    /* Renewal, cancellation and expiration must not remove the other link. */
    subscribe(first, one, 100, false, 2, false, false);
    scan(first);
    subscribe(first, one, 100, false, 0, true, false);
    changed = true; before = packet_count; scan(first);
    assert(packet_count == before + (dual ? 1 : 0));
    if (dual) check_notification(before, "mstp", &mstp, 100, false, 19);
    handler_cov_timer_seconds(30);
    changed = true; before = packet_count; scan(first);
    assert(packet_count == before);
    /* Confirmed notifications and retries retain their link after interleaved reads. */
    subscribe(first, one, 200, true, 60, false, false);
    if (dual) subscribe("mstp", &mstp, 200, true, 60, false, false);
    before = packet_count; scan("mstp");
    assert(packet_count == before + (dual ? 2 : 1));
    uint8_t first_id = check_notification(before, first, one, 200, true, 19);
    uint8_t second_id = dual ? check_notification(before + 1, "mstp", &mstp, 200, true, 19) : 0;
    assert(first_id && (!dual || (second_id && first_id != second_id)));
    discovery_and_reads(first, one);
    if (dual) discovery_and_reads("mstp", &mstp);
    before = packet_count;
    datalink_set("bip");
    tsm_timer_milliseconds(apdu_timeout());
    assert(strcmp(datalink_name(), "bip") == 0 &&
        packet_count == before + (dual ? 2 : 1));
    assert(check_notification(before, first, one, 200, true, 19) == first_id);
    if (dual) assert(check_notification(before + 1, "mstp", &mstp, 200, true, 19) == second_id);
    /* A shared change while both confirmations are pending must reach both after ACK. */
    present_value = 23; changed = true;
    before = packet_count; scan("mstp");
    assert(packet_count == before && !changed);
    ack(first, one, first_id);
    if (dual) ack("mstp", &mstp, second_id);
    before = packet_count; scan("bip");
    assert(packet_count == before + (dual ? 2 : 1));
    first_id = check_notification(before, first, one, 200, true, 23);
    second_id = dual ? check_notification(before + 1, "mstp", &mstp, 200, true, 23) : 0;
    ack(first, one, first_id);
    if (dual) ack("mstp", &mstp, second_id);
    before = packet_count; tsm_timer_milliseconds(apdu_timeout()); scan(first);
    assert(packet_count == before);
    handler_cov_timer_seconds(60);
    present_value = 19;
    /* Address identity is scoped by transport, not inferred from MAC length. */
    if (dual) {
        subscribe("bip", &ip, 300, false, 10, false, false);
        subscribe("mstp", &ip, 300, false, 10, false, false);
        before = packet_count; scan("bip");
        assert(packet_count == before + 2);
        check_notification(before, "bip", &ip, 300, false, 19);
        check_notification(before + 1, "mstp", &ip, 300, false, 19);
        subscribe("bip", &ip, 300, false, 0, true, false);
        changed = true; before = packet_count; scan("bip");
        assert(packet_count == before + 1);
        check_notification(before, "mstp", &ip, 300, false, 19);
        handler_cov_timer_seconds(10);
    }
    /* A failed send must restore the scanner link and leave COV pending. */
    subscribe(first, one, 400, false, 5, false, false);
    fail_send = true; scan("mstp"); fail_send = false;
    before = packet_count; scan("bip");
    assert(packet_count == before + 1);
    check_notification(before, first, one, 400, false, 19);
    handler_cov_timer_seconds(5);
    /* Expiration releases outstanding confirmed transactions on both links. */
    subscribe(first, one, 500, true, 1, false, false);
    if (dual) subscribe("mstp", &mstp, 500, true, 1, false, false);
    before = packet_count; scan("mstp");
    first_id = check_notification(before, first, one, 500, true, 19);
    second_id = dual ? check_notification(before + 1, "mstp", &mstp, 500, true, 19) : 0;
    handler_cov_timer_seconds(1);
    assert(tsm_invoke_id_free(first_id) && (!dual || tsm_invoke_id_free(second_id)));
    before = packet_count; tsm_timer_milliseconds(apdu_timeout()); scan(first);
    assert(packet_count == before);
    /* Failed confirmations exhaust only their own transactions, without changing links. */
    subscribe(first, one, 600, true, 60, false, false);
    if (dual) subscribe("mstp", &mstp, 600, true, 60, false, false);
    before = packet_count; scan("mstp");
    first_id = check_notification(before, first, one, 600, true, 19);
    second_id = dual ? check_notification(before + 1, "mstp", &mstp, 600, true, 19) : 0;
    for (unsigned retry = 0; retry < apdu_retries(); ++retry) {
        before = packet_count;
        datalink_set("bip");
        tsm_timer_milliseconds(apdu_timeout());
        assert(packet_count == before + (dual ? 2 : 1) &&
            strcmp(datalink_name(), "bip") == 0);
        assert(check_notification(before, first, one, 600, true, 19) == first_id);
        if (dual) assert(check_notification(before + 1, "mstp", &mstp, 600, true, 19) == second_id);
    }
    before = packet_count;
    tsm_timer_milliseconds(apdu_timeout());
    assert(packet_count == before && tsm_invoke_id_failed(first_id) &&
        (!dual || tsm_invoke_id_failed(second_id)));
    scan(first);
    assert(tsm_invoke_id_free(first_id) && (!dual || tsm_invoke_id_free(second_id)));
    printf("BACnet %s: discovery, ReadProperty, COV lifecycle, retries and link restoration passed\n", argv[1]);
    return 0;
}
