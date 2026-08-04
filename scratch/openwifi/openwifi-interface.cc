#include <stdexcept>
#include <sstream>

#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <net/if.h>

#include "nl80211.h"
#include "nl80211_testmode_def.h"

#include "openwifi-interface.h"

void nl80211_init(struct nl80211_state *state) {
    state->nl_sock = nl_socket_alloc();
    if (!state->nl_sock) {
        throw std::runtime_error("Failed to allocate netlink socket");
        return;
    }

    std::stringstream err_str;

    nl_socket_set_buffer_size(state->nl_sock, 8192, 8192);

    if (genl_connect(state->nl_sock)) {
        err_str << "Failed to connect to generic netlink.";
        goto out_handle_destroy;
    }

    state->nl80211_id = genl_ctrl_resolve(state->nl_sock, "nl80211");
    if (state->nl80211_id < 0) {
        err_str << "nl80211 not found.";
        goto out_handle_destroy;
    }

    return;

    out_handle_destroy:
        nl_socket_free(state->nl_sock);
        throw std::runtime_error(err_str.str());
}

void nl80211_cleanup(struct nl80211_state *state) {
    nl_socket_free(state->nl_sock);
}

static int error_handler(
        struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg) {
    int *ret = (int*) arg;
    *ret = err->error;
    return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg) {
    int *ret = (int*) arg;
    *ret = 0;
    return NL_SKIP;
}

static int ack_handler(struct nl_msg *msg, void *arg) {
    int *ret = (int*) arg;
    *ret = 0;
    return NL_STOP;
}


OpenwifiInterface::OpenwifiInterface(unsigned int devidx)
{
    m_devidx = devidx;
    nl80211_init(&m_nlstate);
}

OpenwifiInterface::OpenwifiInterface(std::string deviceName)
{
    unsigned int devidx = if_nametoindex(deviceName.c_str());
    m_devidx = devidx;
    nl80211_init(&m_nlstate);
}

OpenwifiInterface::~OpenwifiInterface()
{
    nl80211_cleanup(&m_nlstate);
}

void OpenwifiInterface::set_register(
    unsigned int reg_addr, unsigned int reg_val) {
    int err;
    
    struct nl_msg *msg = nlmsg_alloc();
    if (!msg) {
        throw std::runtime_error("Failed to allocate netlink message");
    }

    bool iw_debug = false;
    nl_cb *cb = nl_cb_alloc(iw_debug ? NL_CB_DEBUG : NL_CB_DEFAULT);
    nl_cb *s_cb = nl_cb_alloc(iw_debug ? NL_CB_DEBUG : NL_CB_DEFAULT);
    if (!cb || !s_cb) {
        throw std::runtime_error("Failed to allocate netlink callbacks");
    }

    genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, m_nlstate.nl80211_id, 0, 0, NL80211_CMD_TESTMODE, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, m_devidx);

    // ==== msg body ====
    struct nlattr *tmdata;
    tmdata = nla_nest_start(msg, NL80211_ATTR_TESTDATA);
    if (!tmdata) {
        throw std::runtime_error("Failed to start new attribute");
        return;
    }
    NLA_PUT_U32(msg, OPENWIFI_ATTR_CMD, REG_CMD_SET);
    NLA_PUT_U32(msg, REG_ATTR_ADDR, reg_addr);
    NLA_PUT_U32(msg, REG_ATTR_VAL,  reg_val);
    goto nla_put_done;

    nla_put_failure:
        throw std::runtime_error("No buffer space available");
        return;
    nla_put_done:
    nla_nest_end(msg, tmdata);

    //printf("reg  cat: %d\n",   reg_addr>>16);
    //printf("reg addr: %08x\n", reg_addr);
    //printf("reg  val: %08x\n", reg_val);

    nl_socket_set_cb(m_nlstate.nl_sock, s_cb);

    err = nl_send_auto(m_nlstate.nl_sock, msg);
    if (err < 0)
        goto out;
    err = 1;

    nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);

    while (err > 0)
        nl_recvmsgs(m_nlstate.nl_sock, cb);
    out:
        nl_cb_put(cb);
    //out_free_msg:
        nlmsg_free(msg);
}

static int cb_reg_handler(struct nl_msg *msg, void *arg) {
    struct nlattr *attrs[NL80211_ATTR_MAX + 1];
    struct nlattr *tb[OPENWIFI_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = (genlmsghdr *) nlmsg_data(nlmsg_hdr(msg));

    //printf("cb_reg_handler\n");

    nla_parse(attrs, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    if (!attrs[NL80211_ATTR_TESTDATA])
        return NL_SKIP;

    nla_parse(tb, OPENWIFI_ATTR_MAX, (nlattr *) nla_data(attrs[NL80211_ATTR_TESTDATA]), nla_len(attrs[NL80211_ATTR_TESTDATA]), NULL);

    // printf("reg  val: %08x\n", nla_get_u32(tb[REG_ATTR_VAL]));
    *(unsigned int *)arg = nla_get_u32(tb[REG_ATTR_VAL]);

    return NL_SKIP;
}

unsigned int OpenwifiInterface::get_register(unsigned int reg_addr) {
    unsigned int read_val = 0;
    int err;
    
    struct nl_msg *msg = nlmsg_alloc();
    if (!msg) {
        throw std::runtime_error("failed to allocate netlink message");
        return 0;
    }

    bool iw_debug = false;
    nl_cb *cb = nl_cb_alloc(iw_debug ? NL_CB_DEBUG : NL_CB_DEFAULT);
    nl_cb *s_cb = nl_cb_alloc(iw_debug ? NL_CB_DEBUG : NL_CB_DEFAULT);
    if (!cb || !s_cb) {
        throw std::runtime_error("failed to allocate netlink callbacks");
        return 0;
    }

    genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, m_nlstate.nl80211_id, 0, 0, NL80211_CMD_TESTMODE, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, m_devidx);

    // ==== msg body ====
    struct nlattr *tmdata;
    tmdata = nla_nest_start(msg, NL80211_ATTR_TESTDATA);
    if (!tmdata) {
        throw std::runtime_error("Failed to start new attribute");
        return 0;
    }

    NLA_PUT_U32(msg, OPENWIFI_ATTR_CMD, REG_CMD_GET);
    NLA_PUT_U32(msg, REG_ATTR_ADDR, reg_addr);

    nla_nest_end(msg, tmdata);

    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, cb_reg_handler, &read_val);
    goto nla_put_done;

    nla_put_failure:
        throw std::runtime_error("No buffer space available");
        return 0;
    nla_put_done:
    nla_nest_end(msg, tmdata);

    // printf("reg  cat: %d\n",   reg_addr>>16);
    // printf("reg addr: %08x\n", reg_addr);

    nl_socket_set_cb(m_nlstate.nl_sock, s_cb);

    err = nl_send_auto(m_nlstate.nl_sock, msg);
    if (err < 0)
        goto out;

    err = 1;

    nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);

    while (err > 0)
        nl_recvmsgs(m_nlstate.nl_sock, cb);
    out:
        nl_cb_put(cb);
    //out_free_msg:
        nlmsg_free(msg);
        return read_val;
}
