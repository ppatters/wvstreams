/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997, 1998 Worldvisions Computer Technology, Inc.
 *
 * Implementation of the WvConfigSection class. 
 *
 * Created:     Sept 28 1997            D. Coombs
 *
 */
#include "wvconf.h"


WvConfigSection::WvConfigSection(const WvString &_name)
	: name(_name)
{
}


WvConfigSection::~WvConfigSection()
{
    // the WvConfigEntryList destructor automatically deletes all its
    // entries, so no need to worry about doing that.
}


WvConfigEntry *WvConfigSection::operator[] (const WvString &ename)
{
    Iter i(*this);

    for (i.rewind(); i.next();)
    {
	if (strcasecmp(i.data()->name, ename) == 0)
	    return i.data();
    }

    return NULL;
}


const char *WvConfigSection::get(const WvString &entry, const char *def_val)
{
    WvConfigEntry *e = (*this)[entry];
    return e ? (char *)e->value : def_val;
}


void WvConfigSection::set(const WvString &entry, const WvString &value)
{
    WvConfigEntry *e = (*this)[entry];
    
    // need to delete the entry?
    if (!value || !value[0])
    {
	if (e) unlink(e);
	return;
    }

    // otherwise, add the entry requested
    if (e)
	e->set(value);
    else
	append(new WvConfigEntry(entry, value), true);
}


void WvConfigSection::dump(FILE *fp)
{
    Iter i(*this);

    for (i.rewind(); i.next(); )
    {
	WvConfigEntry &e = *i.data();
	if (e.value && e.value[0])
	    fprintf(fp, "%s = %s\n", (char *)e.name, (char *)e.value);
	else
	    fprintf(fp, "%s\n", (char *)e.name);
    }
}
