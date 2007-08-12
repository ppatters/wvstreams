/* -*- Mode: C++ -*-
 * Worldvisions Weaver Software:
 *   Copyright (C) 2005-2006 Net Integration Technologies, Inc.
 * 
 * Pathfinder Software:
 *   Copyright (C) 2007, Carillon Information Security Inc.
 *
 * This library is licensed under the LGPL, please read LICENSE for details.
 *
 */ 
#include "wvdbusserver.h"
#include "wvdbuswatch.h"
#include "wvdbuslistener.h"
#include <dbus/dbus.h>


static dbus_bool_t add_watch(DBusWatch *watch, void *data);
static void remove_watch(DBusWatch *watch, void *data);
static void watch_toggled(DBusWatch *watch, void *data);
static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data);
static void remove_timeout(DBusTimeout *timeout, void *data);
static void timeout_toggled(DBusTimeout *timeout, void *data);
    

WvDBusServer::WvDBusServer(WvStringParm addr)
    : cdict(10), rsdict(10),
      log("DBus Server", WvLog::Debug)
{
    DBusError error;
    dbus_error_init(&error);
    dbusserver = dbus_server_listen(addr, &error);
    maybe_seterr(error);
    
    if (!isok())
	return;
    
    dbus_server_set_new_connection_function(dbusserver,
					    new_connection_cb,
					    this, NULL);
    
    if (!dbus_server_set_watch_functions(dbusserver, add_watch, 
					 remove_watch,
					 watch_toggled,
					 this, NULL))
	seterr("Error setting watch functions");
    
    // FIXME: need to add this, timeouts won't work until we do
    dbus_server_set_timeout_functions(dbusserver, add_timeout,
				      remove_timeout, timeout_toggled,
				      this, NULL);
    
    log(WvLog::Info, "Listening on '%s'\n", get_addr());
}


WvDBusServer::~WvDBusServer()
{
    close();
}


void WvDBusServer::maybe_seterr(DBusError &e)
{
    if (dbus_error_is_set(&e))
	seterr_both(EIO, "%s: %s", e.name, e.message);
}


void WvDBusServer::register_conn(WvDBusConn *conn)
{
    assert(!cdict[conn->name]);
    cdict.add(conn, false);
}


WvString WvDBusServer::get_addr()
{
    char *final_addr = dbus_server_get_address(dbusserver);
    WvString faddr(final_addr);
    free(final_addr);
    return faddr;
}


bool WvDBusServer::do_server_msg(WvDBusConn &conn, WvDBusMsg &msg)
{
    if (msg.get_dest() != "org.freedesktop.DBus") return false;
    if (msg.get_path() != "/org/freedesktop/DBus") return false;
    
    // I guess it's for us!
    
    WvString method = msg.get_member();
    if (method == "Hello")
    {
	log("hello_cb\n");
	msg.reply().append(conn.uniquename()).send(conn);
	return true;
    }
    else if (method == "RequestName")
    {
	WvDBusMsg::Iter args(msg);
	WvString _name = args.getnext();
	// uint32_t flags = args.getnext(); // supplied, but ignored
	
	log("request_name_cb(%s)\n", _name);
	conn.name = _name;
	register_conn(&conn);
	assert(cdict[_name] == &conn);
	
	msg.reply().append((uint32_t)DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
	    .send(conn);
	return true;
    }
    else if (method == "ReleaseName")
    {
	WvDBusMsg::Iter args(msg);
	WvString _name = args.getnext();
	
	log("release_name_cb(%s)\n", _name);
	conn.name = "";
	
	msg.reply().append((uint32_t)DBUS_RELEASE_NAME_REPLY_RELEASED)
	    .send(conn);
	return true;
    }
    
    return false; // didn't recognize the method
}


bool WvDBusServer::do_bridge_msg(WvDBusConn &conn, WvDBusMsg &msg)
{
    // if we get here, nobody handled the message internally, so we can try
    // to proxy it.
    if (msg.is_reply())
    {
	WvDBusReplySerial *s = rsdict[msg.get_replyserial()];
	if (s)
	{
	    log("Proxy reply: target is %s\n", s->conn->uniquename());
	    s->conn->send(msg);
	    rsdict.remove(s);
	    return true;
	}
	else
	{
	    log("Proxy reply: unknown serial #%s!\n",
		msg.get_replyserial());
	    // fall through and let someone else look at it
	}
    }
    else if (!!msg.get_dest()) // don't handle blank (broadcast) paths here
    {
	WvDBusConn *dconn = cdict[msg.get_dest()];
	log("Proxying %s -> %s\n",
	    msg, dconn ? dconn->uniquename() : WvString("(UNKNOWN)"));
	if (dconn)
	{
	    uint32_t serial = dconn->send(msg);
	    rsdict.add(new WvDBusReplySerial(serial, &conn), true);
	    log("Proxy: now expecting reply #%s to %s\n",
		serial, conn.uniquename());
	}
	else
	    log(WvLog::Warning,
		"Proxy: no connection for '%s'\n", msg.get_dest());
        return true;
    }
    return false;
}


bool WvDBusServer::do_broadcast_msg(WvDBusConn &conn, WvDBusMsg &msg)
{
    if (!msg.get_dest())
    {
	log("Broadcasting %s\n", msg);
	
	// note: we broadcast messages even back to the connection where
	// they originated.  I'm not sure this is necessarily ideal, but if
	// you don't do that then an app can't signal objects that might be
	// inside itself.
	WvDBusConnDict::Iter i(cdict);
	for (i.rewind(); i.next(); )
	    i->send(msg);
        return true;
    }
    return false;
}


static dbus_bool_t add_watch(DBusWatch *watch, void *data)
{
    WvDBusServer *server = (WvDBusServer *)data;
    
    unsigned int flags = dbus_watch_get_flags(watch);
    WvDBusWatch *wwatch = new WvDBusWatch(watch, flags);
    server->append(wwatch, true, "wvdbuswatch");
    
    dbus_watch_set_data(watch, wwatch, NULL);
    return TRUE;
}

static void remove_watch(DBusWatch *watch, void *data)
{
    WvDBusWatch *wwatch = (WvDBusWatch *)dbus_watch_get_data(watch);
    assert(wwatch);
    wwatch->close();
}

static void watch_toggled(DBusWatch *watch, void *data)
{
    if (dbus_watch_get_enabled(watch))
	add_watch(watch, data);
    else
	remove_watch(watch, data);
}

static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data)
{
    return TRUE;
}

static void remove_timeout(DBusTimeout *timeout, void *data)
{
}

static void timeout_toggled(DBusTimeout *timeout, void *data)
{
}


void WvDBusServer::new_connection_cb(DBusServer *dbusserver, 
				     DBusConnection *new_connection,
				     void *userdata)
{
    static int connect_num = 0;
    
    WvDBusServer *server = (WvDBusServer *)userdata;
    WvDBusConn *c = new WvDBusConn(new_connection,
				   WvString(":%s", ++connect_num));
    
    c->add_callback(WvDBusConn::PriSystem,
		    WvDBusCallback(server, 
				   &WvDBusServer::do_server_msg));
    c->add_callback(WvDBusConn::PriBridge,
		    WvDBusCallback(server, 
				   &WvDBusServer::do_bridge_msg));
    c->add_callback(WvDBusConn::PriBroadcast,
		    WvDBusCallback(server, 
				   &WvDBusServer::do_broadcast_msg));
    
    server->append(c, true, "wvdbus servconn");
    
    if (dbus_connection_get_dispatch_status(new_connection) 
	   != DBUS_DISPATCH_COMPLETE)
	dbus_connection_dispatch(new_connection);
}