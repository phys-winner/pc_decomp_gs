package org.griefsyndrome.port;

import android.net.nsd.NsdServiceInfo;
import java.net.InetAddress;

public final class LanDiscoverySelfTest {
    private static void check(boolean value) {
        if (!value) throw new AssertionError("LAN advertisement validation failed");
    }

    public static void main(String[] args) throws Exception {
        NsdServiceInfo room = new NsdServiceInfo();
        room.setServiceType(LanDiscovery.TYPE + ".");
        room.setHost(InetAddress.getByName("192.168.1.154"));
        room.setPort(6112);
        room.setAttribute("protocol", "1");
        room.setAttribute("players", "2");
        check(LanDiscovery.players(room) == 2);
        room.setServiceType("." + LanDiscovery.TYPE);
        check(LanDiscovery.players(room) == 2);
        room.setAttribute("players", "3");
        check(LanDiscovery.players(room) == 3);
        room.setAttribute("players", "99");
        check(LanDiscovery.players(room) == 0);
        room.setAttribute("players", "2");
        room.setAttribute("protocol", "2");
        check(LanDiscovery.players(room) == 0);
        room.setAttribute("protocol", "1");
        room.setHost(InetAddress.getByName("127.0.0.1"));
        check(LanDiscovery.players(room) == 0);
        room.setHost(InetAddress.getByName("::1"));
        check(LanDiscovery.players(room) == 0);
        room.setHost(InetAddress.getByName("0.0.0.0"));
        check(LanDiscovery.players(room) == 0);
        room.setHost(InetAddress.getByName("224.0.0.1"));
        check(LanDiscovery.players(room) == 0);
        room.setHost(InetAddress.getByName("192.168.1.154"));
        room.setServiceType("_other._udp.");
        check(LanDiscovery.players(room) == 0);
        room.setServiceType(LanDiscovery.TYPE);
        room.setPort(0);
        check(LanDiscovery.players(room) == 0);
        check(LanDiscovery.players(null) == 0);
        System.out.println("PASS: LAN protocol, player count, service type, port and IPv4 validation");
    }
}
