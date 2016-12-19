/*
    EIBD eib bus access and management daemon
    Copyright (C) 2005-2011 Martin Koegler <mkoegler@auto.tuwien.ac.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "busmonitor.h"

A_Busmonitor::~A_Busmonitor ()
{
  TRACEPRINTF (t, 7, this, "Close A_Busmonitor");
  Stop ();
  if (v)
    l3->deregisterVBusmonitor (this);
  else
    l3->deregisterBusmonitor (this);
  while (!data.isempty ())
    {
      delete data.get ();
    }
}

A_Busmonitor::A_Busmonitor (ClientConnection * c, bool virt, bool TS)
{
  TRACEPRINTF (c->t, 7, this, "Open A_Busmonitor");
  this->l3 = c->l3;
  t = c->t;
  con = c;
  v = virt;
  ts = TS;
  pth_sem_init (&sem);
  Start ();
}

void
A_Busmonitor::Send_L_Busmonitor (L_Busmonitor_PDU * l)
{
  data.put (l);
  pth_sem_inc (&sem, 0);
}

void
A_Busmonitor::Run (pth_sem_t * stop1)
{
  CArray resp;

  pth_event_t stop = pth_event (PTH_EVENT_SEM, stop1);
  if (v)
    {
      if (!l3->registerVBusmonitor (this))
	{
	  con->sendreject (stop, EIB_CONNECTION_INUSE);
	  return;
	}
    }
  else
    {
      if (!l3->registerBusmonitor (this))
	{
	  con->sendreject (stop, EIB_CONNECTION_INUSE);
	  return;
	}
    }
  resp.setpart (con->buf, 0, 2);
  if (ts)
    {
      resp.resize (6);
      resp[2] = 0;
      resp[3] = 0;
      resp[4] = 0;
      resp[5] = 0;
    }

  if (con->sendmessage (resp.size(), resp.data(), stop) == -1)
    return;

  pth_event_t sem_ev = pth_event (PTH_EVENT_SEM, &sem);
  while (pth_event_status (stop) != PTH_STATUS_OCCURRED)
    {
      pth_event_concat (sem_ev, stop, NULL);
      pth_wait (sem_ev);
      pth_event_isolate (sem_ev);

      if (pth_event_status (sem_ev) == PTH_STATUS_OCCURRED)
	{
	  pth_sem_dec (&sem);
	  TRACEPRINTF (t, 7, this, "Send Busmonitor-Packet");
	  if (sendResponse (data.get (), stop) == -1)
	    break;
	}
    }
  pth_event_free (sem_ev, PTH_FREE_THIS);
  pth_event_free (stop, PTH_FREE_THIS);
}


void
A_Busmonitor::Do (pth_event_t stop)
{
  while (1)
    {
      if (con->readmessage (stop) == -1)
	break;
      if (EIBTYPE (con->buf) == EIB_RESET_CONNECTION)
	break;
    }
}

int
A_Busmonitor::sendResponse (L_Busmonitor_PDU * p, pth_event_t stop)
{
  CArray buf;
  if (ts)
    {
      buf.resize (7);
      EIBSETTYPE (buf, EIB_BUSMONITOR_PACKET_TS);
      buf[2] = p->status;
      buf[3] = (p->timestamp >> 24) & 0xff;
      buf[4] = (p->timestamp >> 16) & 0xff;
      buf[5] = (p->timestamp >> 8) & 0xff;
      buf[6] = (p->timestamp) & 0xff;
      buf += p->pdu;
    }
  else
    {
      buf.resize (2);
      EIBSETTYPE (buf, EIB_BUSMONITOR_PACKET);
      buf += p->pdu;
    }
  delete p;

  return con->sendmessage (buf.size(), buf.data(), stop);
}

int
A_Text_Busmonitor::sendResponse (L_Busmonitor_PDU * p, pth_event_t stop)
{
  CArray buf;
  String s = p->Decode ();
  buf.resize (2 + s.length() + 1);
  EIBSETTYPE (buf, EIB_BUSMONITOR_PACKET);
  buf.setpart ((uint8_t *)s.c_str(), 2, s.length()+1);
  delete p;

  return con->sendmessage (buf.size(), buf.data(), stop);
}
