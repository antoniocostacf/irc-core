#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "glirc-api.h"

#define CALLBACK_MODULE_KEY "glirc-callback-module"

static void get_glirc_string(lua_State *L, struct glirc_string *s) {
        s->str = lua_tolstring(L, -1, &s->len);
        lua_settop(L, -2);
}

static int glirc_lua_send_message(lua_State *L) {

        luaL_checkany(L, 1);

        size_t netlen = 0;
        const char *net = luaL_checklstring(L, 2, &netlen);

        struct glirc_message msg = { 0 };

        lua_getfield(L, 1, "command");
        get_glirc_string(L, &msg.command);

        lua_getfield(L, 1, "params");
        lua_len(L, -1);
        lua_Integer n = lua_tointeger(L,-1);
        lua_settop(L, -2);

        struct glirc_string params[n];
        msg.params = params;
        msg.params_n = n;

        for (lua_Integer i = 0; i < n; i++) {
                lua_geti(L, 1, i+1);
                get_glirc_string(L, &params[i]);
        }

        void *glirc;
        memcpy(&glirc, lua_getextraspace(L), sizeof(glirc));
        if (glirc_send_message(glirc, net, netlen, &msg)) {
                luaL_error(L, "failure in client");
        }
        return 0;
}

/* Start the Lua interpreter, run glirc.lua in current directory,
 * register the first returned result of running the file as
 * the callback for message processing.
 *
 */
static void *start(void *glirc) {
        lua_State *L = luaL_newstate();
        if (L == NULL) return NULL;
        memcpy(lua_getextraspace(L), &glirc, sizeof(glirc));

        luaL_openlibs(L);

        lua_pushcfunction(L, glirc_lua_send_message);
        lua_setglobal(L, "send_message");

        if (luaL_dofile(L, "glirc.lua")) {
                size_t len = 0;
                const char *msg = lua_tolstring(L, -1, &len);
                glirc_report_error(glirc, msg, len);

                lua_close(L);
                return NULL;
        } else {
                lua_setfield(L, LUA_REGISTRYINDEX, CALLBACK_MODULE_KEY);
                lua_settop(L, 0);
                return L;
        }

}

/* Shutdown the Lua interpreter
 *
 * [-0, +0, -]
 */
static void stop(void * glirc, void * S) {
        if (S == NULL) return;

        lua_State *L = S;
        memcpy(lua_getextraspace(L), &glirc, sizeof(glirc));

        int ty;
        ty = lua_getfield(L, LUA_REGISTRYINDEX, CALLBACK_MODULE_KEY);
        if (ty != LUA_TNIL) {
            ty = lua_getfield(L, -1, "stop");
            if (ty != LUA_TNIL) {
                lua_rotate(L, -2, 1);
                (void)lua_pcall(L, 1, 0, 0);
            }
        }

        lua_close(L);
}

/* Push the string contained in s on the top of the stack
 *
 * [-0, +1, m]
 * */

static void push_glirc_string(lua_State *L, const struct glirc_string *s) {
        lua_pushlstring(L, s->str, s->len);
}

/* Push a table onto the top of the stack containing all of the fields
 * of the message struct
 *
 * [-0, +1, m]
 * */
static void push_glirc_message
  ( lua_State *L
  , const struct glirc_message *msg
  ) {
        lua_createtable(L, 0, 5);

        push_glirc_string(L, &msg->network);
        lua_setfield(L,-2,"network");

        push_glirc_string(L, &msg->prefix);
        lua_setfield(L,-2,"prefix");

        push_glirc_string(L, &msg->command);
        lua_setfield(L,-2,"command");

        { /* populate params */
                const size_t nrec = 0, narr = msg->params_n;
                lua_createtable(L, narr, nrec);

                /* initialize table */
                for (size_t i = 0; i < narr; i++) {
                        push_glirc_string(L, &msg->params[i]);
                        lua_seti(L, -2, i+1);
                }
                lua_setfield(L,-2,"params");
        }

        { /* populate tags */
                const size_t nrec = msg->tags_n, narr = 0;
                lua_createtable(L, narr, nrec);

                /* initialize table */
                for (size_t i = 0; i < nrec; i++) {
                        push_glirc_string(L, &msg->tagkeys[i]);
                        push_glirc_string(L, &msg->tagvals[i]);
                        lua_rawset(L, -3);
                }
                lua_setfield(L,-2,"tags");
        }
}

static void process_message(void *glirc, void * S, const struct glirc_message *msg) {
        if (S == NULL) return;

        lua_State *L = S;
        memcpy(lua_getextraspace(L), &glirc, sizeof(glirc));

        int ty;
        ty = lua_getfield(L, LUA_REGISTRYINDEX, CALLBACK_MODULE_KEY);
        if (ty != LUA_TNIL) {

            ty = lua_getfield(L, -1, "process_message");
            if (ty != LUA_TNIL) {
                lua_rotate(L, -2, 1);
                push_glirc_message(L, msg);
                int res = lua_pcall(L, 2, 0, 0);
                if (res == LUA_ERRRUN) {
                        size_t len = 0;
                        const char *msg = lua_tolstring(L, -1, &len);
                        glirc_report_error(glirc, msg, len);
                }
            }

        }

        lua_settop(L, 0);
}

struct glirc_extension extension = {
        .name            = "Lua",
        .major_version   = 1,
        .minor_version   = 0,
        .start           = start,
        .stop            = stop,
        .process_message = process_message
};
