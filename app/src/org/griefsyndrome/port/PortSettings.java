package org.griefsyndrome.port;

import android.content.Context;
import android.os.Environment;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.util.Properties;
import java.util.HashMap;
import java.util.Map;

public final class PortSettings {
    private static final String GS02_SHA256 = "fae3b559676a2959fad9531937819b1de689da33bb138282a601f4ebcc35d7a3";
    private static final String GS03_SHA256 = "77e1670ccbe8fba32aad275a552cc04d964dffbf26ddef329ba3e6ef58a5072c";
    private static final String EXE_SHA256 = "ef087f503cf9483bc4757b68724007a657992dd7c8a0a416d8c84c1960133124";
    private static final Map<String, String> hashes = new HashMap<>();
    private final File file;
    private final File dataRoot;
    private final File legacyFile;
    private final File legacyDataRoot;
    private final File appFiles;
    private final boolean migrateLegacy;
    private final Properties values = new Properties();

    PortSettings(Context context) {
        this(context, null);
    }

    PortSettings(Context context, String profile) {
        appFiles = context.getExternalFilesDir(null);
        if (appFiles == null) throw new IllegalStateException("External app files are unavailable");
        legacyFile = new File(appFiles, "settings.properties");
        legacyDataRoot = new File(appFiles, "userdata");
        boolean qa = profile != null && !profile.trim().isEmpty();
        if (qa && (!profile.matches("[A-Za-z0-9._-]{1,64}") || profile.equals(".") || profile.equals("..")))
            throw new IllegalArgumentException("Invalid QA profile name");
        File root = new File(Environment.getExternalStorageDirectory(), "GriefSyndrome");
        if (qa) root = new File(root, "qa/" + profile);
        file = new File(root, "settings.properties");
        dataRoot = new File(root, "userdata");
        migrateLegacy = !qa;
        File inputFile = file.isFile() || qa ? file : legacyFile;
        if (inputFile.isFile()) {
            try (FileInputStream input = new FileInputStream(inputFile)) {
                values.load(input);
            } catch (IOException ignored) {
            }
        }
    }

    public static boolean hasRussianData(Context context) {
        try {
            File folder = org.griefsyndrome.port.launcher.Launch.folder(context, "grief", "game");
            return GS02_SHA256.equals(fileHash(new File(folder, "gs02.dat"))) &&
                GS03_SHA256.equals(fileHash(new File(folder, "gs03.dat")));
        } catch (IOException | GeneralSecurityException error) {
            return false;
        }
    }

    public static String gameDataProblem(File folder) {
        if (folder == null) return "Select the game folder";
        for (String name : new String[]{"griefsyndrome.exe", "gs00.dat", "gs01.dat"})
            if (!new File(folder, name).isFile()) return name;
        try {
            if (!EXE_SHA256.equals(fileHash(new File(folder, "griefsyndrome.exe"))))
                return "Requires the original Japanese griefsyndrome.exe v1.00";
            if (new File(folder, "gs00.dat").length() != 637050631L) return "Invalid gs00.dat";
            if (new File(folder, "gs01.dat").length() != 30139900L) return "Invalid gs01.dat";
            return null;
        } catch (IOException | GeneralSecurityException error) {
            return "Cannot read game files: " + error.getMessage();
        }
    }

    private static synchronized String fileHash(File file) throws IOException, GeneralSecurityException {
        String key = file.getAbsolutePath() + ":" + file.length() + ":" + file.lastModified();
        if (hashes.containsKey(key)) return hashes.get(key);
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int read;
            while ((read = input.read(buffer)) >= 0) digest.update(buffer, 0, read);
        }
        byte[] bytes = digest.digest();
        char[] hex = new char[bytes.length * 2];
        char[] alphabet = "0123456789abcdef".toCharArray();
        for (int i = 0; i < bytes.length; ++i) {
            int value = bytes[i] & 0xFF;
            hex[i * 2] = alphabet[value >>> 4];
            hex[i * 2 + 1] = alphabet[value & 15];
        }
        String hash = new String(hex);
        hashes.put(key, hash);
        return hash;
    }

    String language() {
        String value = values.getProperty("language", "ru");
        return value.equals("ru") ? "ru" : "jp";
    }

    void language(String language) {
        values.setProperty("language", language.equals("ru") ? "ru" : "jp");
        save();
    }

    boolean skipLogo() {
        return Boolean.parseBoolean(values.getProperty("skipLogo", "false"));
    }

    boolean showHomuraHint() {
        if (Boolean.parseBoolean(values.getProperty("homuraHintShown", "false"))) return false;
        values.setProperty("homuraHintShown", "true");
        save();
        return true;
    }

    int qaFlags() {
        String stored = values.getProperty("qaFlags");
        if (stored != null) {
            try { return Integer.parseInt(stored) & 7; }
            catch (NumberFormatException error) { return 0; }
        }
        try (FileInputStream input = new FileInputStream(new File(dataRoot, "unlock-stages"))) {
            return input.read() == 1 ? 1 : 0;
        } catch (IOException error) { return 0; }
    }

    void qaFlags(int flags) {
        values.setProperty("qaFlags", Integer.toString(flags & 7));
        save();
    }

    void skipLogo(boolean value) {
        values.setProperty("skipLogo", Boolean.toString(value));
        save();
    }

    float fpsLimit(float fallback) {
        return number("fps", fallback, 0.0f, 1000.0f);
    }

    float fpsLimit() {
        return fpsLimit(0.0f);
    }

    void setFpsLimit(float fps) {
        values.setProperty("fps", Float.toString(fps));
        save();
    }

    private float number(String key, float fallback, float minimum, float maximum) {
        float value;
        try {
            value = Float.parseFloat(values.getProperty(key, Float.toString(fallback)));
        } catch (NumberFormatException error) {
            return fallback;
        }
        if (Float.isNaN(value) || Float.isInfinite(value)) return fallback;
        return Math.max(minimum, Math.min(maximum, value));
    }

    float controlScale() {
        return number("controlScale", 1.0f, 0.65f, 1.6f);
    }

    void controlScale(float scale) {
        values.setProperty("controlScale", Float.toString(scale));
        save();
    }

    float controlSize(String key) {
        return number(key + "Scale", 1.0f, 0.5f, 2.5f);
    }

    void controlSize(String key, float scale) {
        values.setProperty(key + "Scale", Float.toString(scale));
        save();
    }

    float controlAlpha() {
        return number("controlAlpha", 0.65f, 0.15f, 0.9f);
    }

    void controlAlpha(float alpha) {
        values.setProperty("controlAlpha", Float.toString(alpha));
        save();
    }

    float controlMargin() {
        return number("controlMargin", 24.0f, 0.0f, 96.0f);
    }

    void controlMargin(float margin) {
        values.setProperty("controlMargin", Float.toString(margin));
        save();
    }

    float controlX(String key, float fallback) {
        return number(key + "X", fallback, 0.0f, 1.0f);
    }

    float controlY(String key, float fallback) {
        return number(key + "Y", fallback, 0.0f, 1.0f);
    }

    void controlPosition(String key, float x, float y) {
        if (Float.isNaN(x) || Float.isNaN(y) || Float.isInfinite(x) || Float.isInfinite(y)) return;
        values.setProperty(key + "X", Float.toString(Math.max(0.0f, Math.min(1.0f, x))));
        values.setProperty(key + "Y", Float.toString(Math.max(0.0f, Math.min(1.0f, y))));
        save();
    }

    boolean controlLabels() {
        return Boolean.parseBoolean(values.getProperty("controlLabels", "true"));
    }

    void controlLabels(boolean value) {
        values.setProperty("controlLabels", Boolean.toString(value));
        save();
    }

    boolean leftHanded() {
        return Boolean.parseBoolean(values.getProperty("leftHanded", "false"));
    }

    void leftHanded(boolean value) {
        values.setProperty("leftHanded", Boolean.toString(value));
        save();
    }

    boolean controlsAutoHide() {
        return Boolean.parseBoolean(values.getProperty("controlsAutoHide", "false"));
    }

    void controlsAutoHide(boolean value) {
        values.setProperty("controlsAutoHide", Boolean.toString(value));
        save();
    }

    boolean controlsVisible() {
        return values.getProperty("controlsVisible", "true").equals("true");
    }

    void controlsVisible(boolean visible) {
        values.setProperty("controlsVisible", Boolean.toString(visible));
        save();
    }

    boolean lanHost() {
        return Boolean.parseBoolean(values.getProperty("lanHost", "true"));
    }

    void lanHost(boolean value) {
        values.setProperty("lanHost", Boolean.toString(value));
        save();
    }

    String lanAddress() {
        return values.getProperty("lanAddress", "192.168.1.101");
    }

    void lanAddress(String value) {
        values.setProperty("lanAddress", value);
        save();
    }

    int lanPort() {
        try {
            int value = Integer.parseInt(values.getProperty("lanPort", "6112"));
            return value >= 1 && value <= 65535 ? value : 6112;
        } catch (NumberFormatException error) {
            return 6112;
        }
    }

    void lanPort(int value) {
        values.setProperty("lanPort", Integer.toString(value));
        save();
    }

    int lanPlayers() {
        try {
            int value = Integer.parseInt(values.getProperty("lanPlayers", "2"));
            return value >= 2 && value <= 3 ? value : 2;
        } catch (NumberFormatException error) {
            return 2;
        }
    }

    void lanPlayers(int value) {
        values.setProperty("lanPlayers", Integer.toString(value));
        save();
    }

    File gameDirectory() {
        return directory(new File(appFiles, "game"));
    }

    File imagesDirectory() {
        return directory(new File(appFiles, "images"));
    }

    File dataDirectory() {
        return directory(dataRoot);
    }

    void migrateLegacyData() throws IOException {
        directory(dataRoot);
        if (!migrateLegacy) return;
        if (legacyFile.isFile() && !file.exists()) copyFile(legacyFile, file);
        copyTree(legacyDataRoot, dataRoot);
    }

    private static File directory(File directory) {
        if (!directory.isDirectory() && !directory.mkdirs() && !directory.isDirectory()) {
            try {
                android.system.Os.mkdir(directory.getAbsolutePath(), 0775);
            } catch (android.system.ErrnoException error) {
                throw new IllegalStateException("Cannot create " + directory + ": " + error.getMessage(), error);
            }
        }
        if (!directory.isDirectory()) throw new IllegalStateException("Cannot create " + directory);
        return directory;
    }

    private static void copyTree(File source, File target) throws IOException {
        if (!source.exists()) return;
        if (!source.isDirectory()) throw new IOException("Legacy profile is not a directory: " + source);
        directory(target);
        File[] children = source.listFiles();
        if (children == null) throw new IOException("Cannot list legacy profile: " + source);
        for (File child : children) {
            File destination = new File(target, child.getName());
            if (child.isDirectory()) copyTree(child, destination);
            else if (!destination.exists()) copyFile(child, destination);
        }
    }

    private static void copyFile(File source, File target) throws IOException {
        directory(target.getParentFile());
        File temporary = new File(target.getParentFile(), target.getName() + ".part");
        try (FileInputStream input = new FileInputStream(source);
             FileOutputStream output = new FileOutputStream(temporary)) {
            byte[] buffer = new byte[65536];
            int read;
            while ((read = input.read(buffer)) >= 0) output.write(buffer, 0, read);
            output.getFD().sync();
        } catch (IOException error) {
            temporary.delete();
            throw error;
        }
        if (target.exists() || !temporary.renameTo(target)) {
            temporary.delete();
            throw new IOException("Cannot migrate " + source);
        }
    }

    private void save() {
        directory(file.getParentFile());
        File temporary = new File(file.getParentFile(), "settings.properties.part");
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            values.store(output, "Grief Syndrome Android port");
            output.getFD().sync();
            if (!temporary.renameTo(file)) throw new IOException("Cannot replace settings");
        } catch (IOException error) {
            temporary.delete();
            throw new IllegalStateException(error);
        }
    }
}
