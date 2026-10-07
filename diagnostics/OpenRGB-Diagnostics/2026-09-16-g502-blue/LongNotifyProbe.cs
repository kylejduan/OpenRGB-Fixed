using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

// Read-only listener on the Powerplay receiver's short-report (usage 1) collection.
public sealed class LongNotifyProbe : IDisposable
{
    const string Dll = @"C:\OpenRGB-Diagnostics\2026-09-16-g502-blue\hidapi\hidapi.dll";
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
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern int hid_read_timeout(IntPtr dev, byte[] data, UIntPtr size, int timeout);
    [DllImport(Dll, CallingConvention=CallingConvention.Cdecl)] static extern int hid_write(IntPtr dev, byte[] data, UIntPtr size);
    IntPtr handle;

    public LongNotifyProbe()
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
        if(paths.Count!=1) { hid_exit(); throw new Exception("Expected exactly one Powerplay long-report interface, found "+paths.Count); }
        handle=hid_open_path(paths[0]);
        if(handle==IntPtr.Zero) { hid_exit(); throw new Exception("Cannot open Powerplay long-report interface"); }
    }

    // Returns the hex of one report, "" on timeout, throws on read error.
    public string Next(int timeoutMs)
    {
        byte[] buffer=new byte[64];
        int n=hid_read_timeout(handle,buffer,(UIntPtr)buffer.Length,timeoutMs);
        if(n<0) throw new Exception("HID read failed");
        return n==0 ? "" : BitConverter.ToString(buffer,0,n);
    }

    public void Write(byte[] report)
    {
        if(hid_write(handle,report,(UIntPtr)report.Length)!=report.Length) throw new Exception("HID write failed");
    }

    public void Dispose()
    {
        if(handle!=IntPtr.Zero) { hid_close(handle); handle=IntPtr.Zero; hid_exit(); }
    }
}
