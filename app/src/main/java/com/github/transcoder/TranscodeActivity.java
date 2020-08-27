package com.github.transcoder;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.support.annotation.Nullable;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.TextView;

import com.github.transcoder.bean.VideoInfo;
import com.github.transcoder.jni.FFmpegCmd;
import com.github.transcoder.util.Gutil;
import com.github.transcoder.util.MediaTool;

import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Locale;

public class TranscodeActivity extends AppCompatActivity implements View.OnClickListener {

    private final static int CODE_REQUEST_WRITE_EXTERNAL = 0x100;
    private int PICK_VIDEO_REQUEST = 0x2;
    private String mVideoPath;
    private ImageView mIvCover;
    private TextView mTvResolution;
    private TextView mTvFps;
    private TextView mTvBitrate;
    private TextView mTvDuration;
    private TextView mTvVcodec;
    private TextView mTvRotation;

    private VideoInfo mInfo;
    private Handler mHandler = new Handler(Looper.getMainLooper());

    private Button mBtnStartDecode;


    public static String getDetailTime() {
        SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd-HH-mm-ss", Locale.getDefault());
        return sdf.format(System.currentTimeMillis());
    }

    public static String getVideoPath() {
        String path = null;
        File folder = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES);
        if (!folder.exists()) {
            boolean mkdirs = folder.mkdirs();
        }
        path = folder.getAbsolutePath();
        if (!path.endsWith("/")) {
            return path + "/";
        } else {
            return path;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_transcode);

        mIvCover = findViewById(R.id.iv_cover);
        mIvCover.setOnClickListener(this);

        mBtnStartDecode = findViewById(R.id.btn_start_decode);
        mBtnStartDecode.setOnClickListener(this::onClick);

        mTvResolution = findViewById(R.id.tv_resolution);
        mTvFps = findViewById(R.id.tv_fps);
        mTvBitrate = findViewById(R.id.tv_bitrate);
        mTvDuration = findViewById(R.id.tv_duration);
        mTvVcodec = findViewById(R.id.tv_vcodec);
        mTvRotation = findViewById(R.id.tv_rotation);

        checkPermission();
    }

    private void checkPermission() {
        int permissions = ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE);
        if (permissions != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    CODE_REQUEST_WRITE_EXTERNAL
            );
        }

    }

    @Override
    public void onClick(View view) {
        switch (view.getId()) {
            case R.id.iv_cover:
                Intent i = new Intent(Intent.ACTION_PICK, MediaStore.Video.Media.EXTERNAL_CONTENT_URI);
                startActivityForResult(i, PICK_VIDEO_REQUEST);
                break;
            case R.id.btn_start_decode:
//                mVideoPath = "rtmp://58.200.131.2:1935/livetv/hunantv";
                mVideoPath = "rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov";
                new Thread(this::decode).start();
                updateVideo();



        }
    }

    private void decode(){
        FFmpegCmd.videoDecode(mVideoPath);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        if (requestCode == PICK_VIDEO_REQUEST && resultCode == RESULT_OK && null != data) {
            Uri selectedVideo = data.getData();
            String[] filePathColumn = {MediaStore.Video.Media.DATA};

            Cursor cursor = getContentResolver().query(selectedVideo,
                    filePathColumn, null, null, null);
            cursor.moveToFirst();

            int columnIndex = cursor.getColumnIndex(filePathColumn[0]);
            mVideoPath = cursor.getString(columnIndex);
//            mVideoPath = "/sdcard/Download/gamepp/KSP.mkv";
            cursor.close();
            updateVideo();
        }
    }

    private void updateVideo() {
        //Bitmap videoFrame = MediaTool.getVideoFrame(mVideoPath, 2000000);
        //mIvCover.setImageBitmap(videoFrame);
        new Thread(){
            @Override
            public void run() {
                mInfo = FFmpegCmd.getVideoInfo(mVideoPath);
                runOnUiThread(()->{
                    mTvResolution.setText("分辨率：" + mInfo.width + "x" + mInfo.height);
                    mTvBitrate.setText("码率：" + Gutil.bitrateFormat(mInfo.bitrate));
                    mTvFps.setText("FPS：" + mInfo.fps);
                    mTvDuration.setText("视频时长：" + Gutil.parseTime((int) mInfo.duration/1000));
                    mTvVcodec.setText("Video Codec: " + mInfo.videoCodec);
                    mTvRotation.setText("Video Rotation: " + mInfo.rotation+"°");
                });
            }
        }.start();

    }
}
