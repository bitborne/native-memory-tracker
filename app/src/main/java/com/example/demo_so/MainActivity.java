package com.example.demo_so;

import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import java.io.File;

public class MainActivity extends AppCompatActivity {

    // 加载 so1 库
    static {
        System.loadLibrary("demo_so");
        System.loadLibrary("so2");
    }
 
    // 声明 native 方法
    public native void nativeStartSimulation();
    public native void nativeStopSimulation();
 
    private TextView tvStatus;
    private Button btnToggle;
    private boolean isRunning = false;

    // 声明 so2 的 native 方法
    public native void nativeInitHook(String logPath);
    public native void nativeCloseLog();

    // Idle Page Monitor 控制
    public native void nativeStartIdleMonitor();
    public native void nativeStopIdleMonitor();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // 初始化 so2（在开始模拟之前）
        File logFile = new File(getExternalFilesDir(null), "mem_reg.log");
        nativeInitHook(logFile.getAbsolutePath());
        Log.i("SO2", "Log file: " + logFile.getAbsolutePath());

        tvStatus = findViewById(R.id.tv_status);
        btnToggle = findViewById(R.id.btn_toggle);
 
        btnToggle.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                toggleSimulation();
            }
        });
    }
 
    private void toggleSimulation() {
        if (!isRunning) {
            nativeStartSimulation();
            nativeStartIdleMonitor();  // 启动 Idle Page 监控
            tvStatus.setText("运行中...\n\n" +
                    "L1-HOT: 渲染缓冲(永不释放)\n" +
                    "L2-WARM: 对象池(高频分配/释放)\n" +
                    "L3-COOL: 配置缓存(5秒清理)\n" +
                    "L4-COLD: 资源包(mmap 30秒)\n\n" +
                    "IdlePage: 监控中(10ms-1s自适应)");
            btnToggle.setText("停止模拟");
            isRunning = true;
        } else {
            nativeStopIdleMonitor();  // 停止 Idle Page 监控
            nativeStopSimulation();
            nativeCloseLog();  // 停止后立即关闭日志，确保数据写入
            tvStatus.setText("已停止");
            btnToggle.setText("开始模拟");
            isRunning = false;
        }
    }
 
    @Override
    protected void onDestroy() {
        if (isRunning) {
            nativeStopIdleMonitor();  // 确保停止 Idle Page 监控
            nativeStopSimulation();
        }
        nativeCloseLog();  // 关闭日志
        super.onDestroy();
    }
}
