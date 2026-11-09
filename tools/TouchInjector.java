import android.hardware.input.InputManager;
import android.os.SystemClock;
import android.view.MotionEvent;
import java.lang.reflect.Method;

public final class TouchInjector {
    private static Object manager;
    private static Method inject;

    private static void initialize() throws Exception {
        Class<?> services = Class.forName("android.os.ServiceManager");
        Object binder = services.getMethod("getService", String.class).invoke(null, "input");
        Class<?> stub = Class.forName("android.hardware.input.IInputManager$Stub");
        Class<?> binderType = Class.forName("android.os.IBinder");
        manager = stub.getMethod("asInterface", binderType).invoke(null, binder);
        Class<?> type = Class.forName("android.hardware.input.IInputManager");
        inject = type.getMethod("injectInputEvent", android.view.InputEvent.class, Integer.TYPE);
    }

    private static void send(long downTime, int action, int actionIndex, float[][] points) throws Exception {
        MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[points.length];
        MotionEvent.PointerCoords[] coordinates = new MotionEvent.PointerCoords[points.length];
        for (int i = 0; i < points.length; ++i) {
            properties[i] = new MotionEvent.PointerProperties();
            properties[i].id = i;
            properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            coordinates[i] = new MotionEvent.PointerCoords();
            coordinates[i].x = points[i][0];
            coordinates[i].y = points[i][1];
            coordinates[i].pressure = 1.0f;
            coordinates[i].size = 1.0f;
        }
        MotionEvent event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(),
            action | (actionIndex << MotionEvent.ACTION_POINTER_INDEX_SHIFT), points.length,
            properties, coordinates, 0, 0, 1.0f, 1.0f, 1, 0,
            android.view.InputDevice.SOURCE_TOUCHSCREEN, 0);
        Object result = inject.invoke(manager, event, 1);
        event.recycle();
        if (!Boolean.TRUE.equals(result)) throw new IllegalStateException("InputManager rejected touch event");
    }

    private static long down(float x, float y) throws Exception {
        long time = SystemClock.uptimeMillis();
        send(time, MotionEvent.ACTION_DOWN, 0, new float[][]{{x, y}});
        return time;
    }

    private static void up(long time, float x, float y) throws Exception {
        send(time, MotionEvent.ACTION_UP, 0, new float[][]{{x, y}});
    }

    private static void tap(float x, float y) throws Exception {
        long time = down(x, y);
        SystemClock.sleep(80);
        up(time, x, y);
    }

    private static void hold(float x, float y, long milliseconds) throws Exception {
        long time = down(x, y);
        SystemClock.sleep(milliseconds);
        up(time, x, y);
    }

    private static void drag(float x1, float y1, float x2, float y2, long milliseconds) throws Exception {
        long time = down(x1, y1);
        int steps = Math.max(1, (int)(milliseconds / 16));
        for (int step = 1; step <= steps; ++step) {
            float fraction = (float)step / steps;
            send(time, MotionEvent.ACTION_MOVE, 0, new float[][]{{x1 + (x2 - x1) * fraction, y1 + (y2 - y1) * fraction}});
            SystemClock.sleep(16);
        }
        up(time, x2, y2);
    }

    private static void pinch(float x, float y, float first, float second, long milliseconds) throws Exception {
        float[][] points = {{x - first / 2, y}, {x + first / 2, y}};
        long time = SystemClock.uptimeMillis();
        send(time, MotionEvent.ACTION_DOWN, 0, new float[][]{points[0]});
        send(time, MotionEvent.ACTION_POINTER_DOWN, 1, points);
        int steps = Math.max(1, (int)(milliseconds / 16));
        for (int step = 1; step <= steps; ++step) {
            float distance = first + (second - first) * step / steps;
            points[0][0] = x - distance / 2;
            points[1][0] = x + distance / 2;
            send(time, MotionEvent.ACTION_MOVE, 0, points);
            SystemClock.sleep(16);
        }
        send(time, MotionEvent.ACTION_POINTER_UP, 0, points);
        send(time, MotionEvent.ACTION_UP, 0, new float[][]{points[1]});
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 1 && args[0].equals("methods")) {
            Class<?> type = Class.forName("android.hardware.input.IInputManager");
            for (Method method : type.getDeclaredMethods()) System.out.println(method.toGenericString());
            return;
        }
        if (args.length == 3 && args[0].equals("cancel")) {
            initialize();
            float x = Float.parseFloat(args[1]), y = Float.parseFloat(args[2]);
            long time = down(x, y);
            SystemClock.sleep(100);
            send(time, MotionEvent.ACTION_CANCEL, 0, new float[][]{{x, y}});
            return;
        }
        if (args.length < 1) throw new IllegalArgumentException("tap X Y | hold X Y MS | drag X1 Y1 X2 Y2 MS | pinch CX CY START END MS | cancel");
        initialize();
        SystemClock.sleep(1000);
        String command = args[0];
        if (command.equals("tap") && args.length == 3) {
            tap(Float.parseFloat(args[1]), Float.parseFloat(args[2]));
        } else if (command.equals("hold") && args.length == 4) {
            hold(Float.parseFloat(args[1]), Float.parseFloat(args[2]), Long.parseLong(args[3]));
        } else if (command.equals("drag") && args.length == 6) {
            drag(Float.parseFloat(args[1]), Float.parseFloat(args[2]), Float.parseFloat(args[3]),
                Float.parseFloat(args[4]), Long.parseLong(args[5]));
        } else if (command.equals("pinch") && args.length == 6) {
            pinch(Float.parseFloat(args[1]), Float.parseFloat(args[2]), Float.parseFloat(args[3]),
                Float.parseFloat(args[4]), Long.parseLong(args[5]));
        } else {
            throw new IllegalArgumentException("Invalid touch command");
        }
        System.out.println(command + " injected");
    }
}
