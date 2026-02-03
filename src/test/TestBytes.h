#pragma once
#include "TestRunner.h"

inline void registerBytesTests(TestRunner &runner) {

  runner.addTest("Bytes: Static Constructors",
                 R"(
            var b1 = Bytes.create(5); 
            print(b1.length);
            
            var b2 = Bytes.fromList([65, 66, 67]);
            print(b2.toStr());
            
            var s = "SPT";
            var b3 = Bytes.fromStr(s);
            print(b3.length);
            print(b3.readUInt8(0));
       )",
                 "5\nABC\n3\n83");

  runner.addTest("Bytes: Container Operations",
                 R"(
            var b = Bytes.create(0);
            b.push(10);
            b.push(20);
            print(b.length);
            print(b.pop());
            
            b.resize(5);
            b.fill(255, 0, 5);
            print(b.readUInt8(0));
            print(b.readUInt8(4));
            
            b.clear();
            print(b.length);
       )",
                 "2\n20\n255\n255\n0");

  runner.addTest("Bytes: Hex Conversions",
                 R"(
            var b = Bytes.fromHex("48 65 6c 6c 6f"); 
            print(b.toStr());
            print(b.toHex());
       )",
                 "Hello\n48656C6C6F");

  runner.addTest("Bytes: Binary Read/Write (Endianness)",
                 R"(
            var b = Bytes.create(8);
            
            b.writeInt32(0, 305419896); 
            print(b.readUInt8(0)); 
            
            print(b.readInt32(0, false));
            
            b.writeInt16(4, 4660, true);
            print(b.readUInt8(4));
            print(b.readUInt16(4, true));
       )",
                 "120\n305419896\n18\n4660");

  runner.addTest("Bytes: Float & Double",
                 R"(
            var b = Bytes.create(16);
            b.writeFloat(0, 3.14);
            var f = b.readFloat(0);
            print(f > 3.13 && f < 3.15); 
            
            b.writeDouble(8, 1234.5678);
            print(b.readDouble(8));
       )",
                 "true\n1234.5678");

  runner.addTest("Bytes: String Operations",
                 R"(
            var b = Bytes.create(20);
            var written = b.writeString(0, "SPT-LANG");
            print(written);
            print(b.readString(0, 3));
            b.writeString(4, "XXXX");
            print(b.readString(0, 8));
       )",
                 "8\nSPT\nSPT-XXXX");

  runner.addTest("Bytes: Slicing",
                 R"(
            bytes b = Bytes.fromList([1, 2, 3, 4, 5]);
            
            bytes sub = b.slice(1, 4);
            print(sub.length);
            print(sub.readUInt8(0));
            
            bytes sub2 = b.slice(3, 5);
            print(sub2.length);
            if (sub2.length > 0) {
                print(sub2.readUInt8(0));
            } else {
                print("empty");
            }
       )",
                 "3\n2\n2\n4");

  runner.addTest("Bytes: Safety & Error Handling",
                 R"(
            var ok = pcall(Bytes.create, -1);
            print(ok);
            
            ok = pcall(Bytes.fromHex, "123");
            print(ok);
            
            ok = pcall(Bytes.fromList, [1, "bad", 3]);
            print(ok);

            var b = Bytes.create(4);
            ok = pcall(b.readInt32, 4);
            print(ok);
            
            ok = pcall(b.writeInt8, 10, 255);
            print(ok);
            
            print(b.length);
       )",
                 "false\nfalse\nfalse\nfalse\nfalse\n4");

  runner.addTest("Bytes: Struct Packing (Network Packet Sim)",
                 R"(
            var packet = Bytes.create(11);
            
            packet.writeUInt8(0, 0xAA);
            packet.writeUInt16(1, 1024, true);
            
            packet.writeUInt32(3, 3735928559, true); 
            packet.writeFloat(7, 1.5);
            
            print(packet.readUInt8(0)); 
            print(packet.readUInt16(1, true));
            print(packet.readUInt16(1, false)); 
            
            
            print(packet.readUInt16(3, true));
       )",
                 "170\n1024\n4\n57005");

  runner.addTest("Bytes: Advanced Buffer Ops",
                 R"(
            var b = Bytes.fromList([257, 512, 255]);
            print(b.readUInt8(0));
            print(b.readUInt8(1));
            print(b.readUInt8(2));
            
            b.resize(10);
            b.fill(0, 0, 10);
            b.fill(65, 2, 5);
            print(b.readUInt8(1));
            print(b.readUInt8(2));
            print(b.readUInt8(4));
            print(b.readUInt8(5));
            
            b.writeString(6, "XYZ");
            var s = b.readString(6, 100);
            print(s.length);
            
            b.resize(2);
            print(b.length);
            
            var ok = pcall(b.readUInt8, 2);
            print(ok);
       )",
                 "1\n0\n255\n0\n65\n65\n0\n4\n2\nfalse");

  runner.addTest("Bytes: Loop & Stack Logic",
                 R"(
            var b = Bytes.create(0);
            for (var i = 0; i < 100; i = i + 1) {
                b.push(i);
            }
            print(b.length);
            print(b.readUInt8(50));
            
            var sum = 0;
            for (var i = 0; i < 10; i = i + 1) {
                sum = sum + b.pop();
            }
            print(sum);
            print(b.length);
       )",
                 "100\n50\n945\n90");
}