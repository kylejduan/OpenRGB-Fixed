using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

public sealed class HidppProbe : IDisposable
{
    const string Dll = @"C:\Program Files\OpenRGB\hidapi.dll";
    [StructLayout(LayoutKind.Sequential)]
    struct Info
    {
        public IntPtr Path;
        public ushort Vendor, Product;
        public IntPtr Serial;
        public ushort Release;
        public IntPtr Manufacturer, ProductName;
        public ushort UsagePage, Usage;
        public int Interface;
        public IntPtr Next;
        public int Bus;
    }
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern int hid_init();
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern int hid_exit();
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr hid_enumerate(ushort vendor, ushort product);
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern void hid_free_enumeration(IntPtr first);
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Ansi)] static extern IntPtr hid_open_path(string path);
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern void hid_close(IntPtr dev);
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern int hid_write(IntPtr dev, byte[] data, UIntPtr size);
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern int hid_read_timeout(IntPtr dev, byte[] data, UIntPtr size, int timeout);
    IntPtr handle;
    public readonly List<string> Trace = new List<string>();

    public HidppProbe()
    {
        if(hid_init() != 0) throw new Exception("hid_init failed");
        IntPtr first=hid_enumerate(0x046D,0xC53A);
        var paths=new List<string>();
        try
        {
            for(IntPtr p=first;p!=IntPtr.Zero;)
            {
                Info info=(Info)Marshal.PtrToStructure(p,typeof(Info));
                if(info.UsagePage==0xFF00 && info.Usage==2)
                    paths.Add(Marshal.PtrToStringAnsi(info.Path));
                p=info.Next;
            }
        }
        finally { hid_free_enumeration(first); }
        if(paths.Count!=1) { hid_exit(); throw new Exception("Expected exactly one Powerplay long-report interface"); }
        handle=hid_open_path(paths[0]);
        if(handle==IntPtr.Zero) { hid_exit(); throw new Exception("Cannot open Powerplay HID interface"); }
    }

    public byte[] Request(byte slot, byte feature, byte function, byte[] data)
    {
        if(data.Length>16 || (function&15)!=0) throw new ArgumentException("Invalid HID++ request");
        byte[] packet=new byte[20];
        packet[0]=0x11;packet[1]=slot;packet[2]=feature;packet[3]=(byte)(function|0x0C);
        Array.Copy(data,0,packet,4,data.Length);
        Trace.Add("TX "+BitConverter.ToString(packet));
        if(hid_write(handle,packet,(UIntPtr)packet.Length)!=packet.Length) throw new Exception("HID write failed");
        var clock=Stopwatch.StartNew();
        while(clock.ElapsedMilliseconds<900)
        {
            byte[] buffer=new byte[64];
            int n=hid_read_timeout(handle,buffer,(UIntPtr)buffer.Length,100);
            if(n<0) throw new Exception("HID read failed");
            if(n<4) continue;
            Trace.Add("RX "+BitConverter.ToString(buffer,0,n));
            if(buffer[1]!=slot) continue;
            if((buffer[2]==0xFF || buffer[2]==0x8F) && n>=6 && buffer[3]==feature && buffer[4]==packet[3])
                throw new Exception("HID++ error "+buffer[5].ToString("X2"));
            if(buffer[2]==feature && buffer[3]==packet[3])
            {
                byte[] result=new byte[n-4];Array.Copy(buffer,4,result,0,result.Length);return result;
            }
        }
        throw new Exception("No matching HID++ reply before deadline");
    }

    public void Dispose()
    {
        if(handle!=IntPtr.Zero) { hid_close(handle); handle=IntPtr.Zero; hid_exit(); }
    }
}
