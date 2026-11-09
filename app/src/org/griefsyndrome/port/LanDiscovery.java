package org.griefsyndrome.port;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.net.Inet4Address;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

final class LanDiscovery implements AutoCloseable {
    static final String TYPE = "_griefsyndrome._udp";
    static final int PORT = 6112;

    interface Listener {
        void status(String text);
        default void found(NsdServiceInfo service) {}
        default void lost(String name) {}
    }

    private final NsdManager manager;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ArrayDeque<NsdServiceInfo> pending = new ArrayDeque<>();
    private final Set<String> present = new HashSet<>();
    private NsdManager.DiscoveryListener discovery;
    private NsdManager.RegistrationListener registration;
    private boolean resolving;
    private volatile boolean closed;

    LanDiscovery(Context context, Listener listener) {
        manager = (NsdManager) context.getSystemService(Context.NSD_SERVICE);
        this.listener = listener;
    }

    private void status(String text) {
        handler.post(() -> { if (!closed) listener.status(text); });
    }

    static int players(NsdServiceInfo service) {
        if (service == null || service.getServiceType() == null ||
            !(TYPE.equalsIgnoreCase(service.getServiceType()) || (TYPE + ".").equalsIgnoreCase(service.getServiceType()) ||
              ("." + TYPE).equalsIgnoreCase(service.getServiceType())) ||
            !(service.getHost() instanceof Inet4Address) || service.getHost().isAnyLocalAddress() ||
            service.getHost().isLoopbackAddress() || service.getHost().isMulticastAddress() ||
            service.getPort() < 1 || service.getPort() > 65535 ||
            !Arrays.equals(service.getAttributes().get("protocol"), new byte[] {'1'})) return 0;
        byte[] value = service.getAttributes().get("players");
        return value != null && value.length == 1 && (value[0] == '2' || value[0] == '3') ? value[0] - '0' : 0;
    }

    void advertise(int port, int players) {
        NsdServiceInfo service = new NsdServiceInfo();
        service.setServiceName("Grief Syndrome · " + Build.MODEL);
        service.setServiceType(TYPE);
        service.setPort(port);
        service.setAttribute("protocol", "1");
        service.setAttribute("players", Integer.toString(players));
        registration = new NsdManager.RegistrationListener() {
            @Override public void onServiceRegistered(NsdServiceInfo info) {
                handler.post(() -> {
                    Log.i("GriefSyndrome", "LAN advertised: " + info.getServiceName() + " port " + port);
                    if (closed) unregister();
                    else listener.status("Игра создана. Ожидание клиента в этой Wi-Fi сети…");
                });
            }
            @Override public void onRegistrationFailed(NsdServiceInfo info, int error) {
                Log.w("GriefSyndrome", "LAN registration failed: " + error);
                status("Не удалось объявить игру в сети. Проверьте Wi-Fi и повторите подключение.");
            }
            @Override public void onServiceUnregistered(NsdServiceInfo info) {}
            @Override public void onUnregistrationFailed(NsdServiceInfo info, int error) {
                Log.w("GriefSyndrome", "LAN unregister failed: " + error);
            }
        };
        try {
            manager.registerService(service, NsdManager.PROTOCOL_DNS_SD, registration);
        } catch (RuntimeException error) {
            Log.w("GriefSyndrome", "Cannot advertise LAN game", error);
            status("Не удалось объявить игру. Разрешите доступ к локальной сети и проверьте Wi-Fi.");
        }
    }

    void search() {
        discovery = new NsdManager.DiscoveryListener() {
            @Override public void onDiscoveryStarted(String type) { status("Поиск комнат в этой Wi-Fi сети…"); }
            @Override public void onServiceFound(NsdServiceInfo info) {
                handler.post(() -> {
                    if (closed || !present.add(info.getServiceName())) return;
                    pending.add(info);
                    resolveNext();
                });
            }
            @Override public void onServiceLost(NsdServiceInfo info) {
                handler.post(() -> {
                    present.remove(info.getServiceName());
                    pending.removeIf(item -> item.getServiceName().equals(info.getServiceName()));
                    if (!closed) listener.lost(info.getServiceName());
                });
            }
            @Override public void onDiscoveryStopped(String type) {}
            @Override public void onStartDiscoveryFailed(String type, int error) {
                handler.post(() -> {
                    if (closed) return;
                    Log.w("GriefSyndrome", "LAN discovery failed: " + error);
                    listener.status("Не удалось начать поиск. Разрешите доступ к локальной сети и проверьте Wi-Fi.");
                    stopSearch();
                });
            }
            @Override public void onStopDiscoveryFailed(String type, int error) {
                Log.w("GriefSyndrome", "LAN stop discovery failed: " + error);
            }
        };
        try {
            manager.discoverServices(TYPE, NsdManager.PROTOCOL_DNS_SD, discovery);
        } catch (RuntimeException error) {
            Log.w("GriefSyndrome", "Cannot discover LAN games", error);
            status("Не удалось начать поиск. Разрешите доступ к локальной сети и проверьте Wi-Fi.");
        }
    }

    private void resolveNext() {
        if (closed || resolving || pending.isEmpty()) return;
        NsdServiceInfo service = pending.removeFirst();
        resolving = true;
        try {
            manager.resolveService(service, new NsdManager.ResolveListener() {
                @Override public void onServiceResolved(NsdServiceInfo info) {
                    handler.post(() -> {
                        resolving = false;
                        if (closed) return;
                        int count = players(info);
                        if (count != 0 && present.contains(service.getServiceName())) {
                            Log.i("GriefSyndrome", "LAN found: " + info.getHost().getHostAddress() + ":" + info.getPort());
                            listener.found(info);
                        }
                        resolveNext();
                    });
                }
                @Override public void onResolveFailed(NsdServiceInfo info, int error) {
                    handler.post(() -> {
                        resolving = false;
                        if (closed) return;
                        resolveNext();
                        handler.postDelayed(() -> {
                            if (!closed && present.contains(service.getServiceName())) {
                                pending.add(service);
                                resolveNext();
                            }
                        }, 1500);
                    });
                }
            });
        } catch (RuntimeException error) {
            resolving = false;
            Log.w("GriefSyndrome", "Cannot resolve LAN game", error);
            status("Не удалось определить адрес хоста. Повторите поиск.");
        }
    }

    private void stopSearch() {
        if (discovery == null) return;
        try { manager.stopServiceDiscovery(discovery); }
        catch (IllegalArgumentException error) { Log.d("GriefSyndrome", "LAN search already stopped", error); }
        discovery = null;
    }

    private void unregister() {
        if (registration == null) return;
        try { manager.unregisterService(registration); }
        catch (IllegalArgumentException error) { Log.d("GriefSyndrome", "LAN advertisement already stopped", error); }
        registration = null;
    }

    @Override public void close() {
        if (closed) return;
        closed = true;
        handler.removeCallbacksAndMessages(null);
        stopSearch();
        unregister();
        pending.clear();
        present.clear();
    }
}
