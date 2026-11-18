#!/usr/bin/env python3
"""Host-only TLS 1.2 end-to-end test. Uses an ephemeral CA and localhost.

The generated private keys live only in a temporary directory, never in the
kernel image, tracked sources, or distributable archives.
"""
import socket
import ssl
import subprocess
import tempfile
import threading
from datetime import datetime, timezone
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'compiled'

def array(name, data):
    return 'static const unsigned char %s[] = {%s};\n' % (
        name, ','.join('0x%02x' % b for b in data))

with tempfile.TemporaryDirectory(prefix='tiny64_tls_test_') as tmp:
    key_ca = rsa.generate_private_key(public_exponent=65537,key_size=2048)
    key_leaf = rsa.generate_private_key(public_exponent=65537,key_size=2048)
    root_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'Tiny64 integration CA')])
    leaf_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'tiny64-test.invalid')])
    start = datetime(2025,1,1,tzinfo=timezone.utc)
    end = datetime(2035,1,1,tzinfo=timezone.utc)
    root = (x509.CertificateBuilder().subject_name(root_name).issuer_name(root_name)
        .public_key(key_ca.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(start).not_valid_after(end)
        .add_extension(x509.BasicConstraints(ca=True,path_length=1),critical=True)
        .sign(key_ca,hashes.SHA256()))
    leaf = (x509.CertificateBuilder().subject_name(leaf_name).issuer_name(root_name)
        .public_key(key_leaf.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(start).not_valid_after(end)
        .add_extension(x509.BasicConstraints(ca=False,path_length=None),critical=True)
        .add_extension(x509.SubjectAlternativeName([x509.DNSName('tiny64-test.invalid')]),critical=False)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),critical=False)
        .sign(key_ca,hashes.SHA256()))
    Path(tmp,'server.crt').write_bytes(leaf.public_bytes(serialization.Encoding.PEM))
    Path(tmp,'server.key').write_bytes(key_leaf.private_bytes(
        serialization.Encoding.PEM,serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.NoEncryption()))
    public = key_ca.public_key().public_numbers()
    OUT.mkdir(exist_ok=True)
    (OUT/'generated_https_integration.h').write_text(
        array('test_root_dn',root.subject.public_bytes()) +
        array('test_root_n',public.n.to_bytes((public.n.bit_length()+7)//8,'big')) +
        array('test_root_e',public.e.to_bytes((public.e.bit_length()+7)//8,'big')))
    objects = sorted(str(p) for p in (OUT/'vendor/bearssl/src').rglob('*.o'))
    subprocess.run(['gcc','-no-pie','-std=c11','-O1','-Wall','-Wextra','-Werror',
                    '-Ivendor/bearssl/inc','-Icompiled',
                    'tests/test_https_integration.c',
                    'compiled/tls/tls_probe.o','compiled/tls/https_preview.o',
                    *objects,'-o','compiled/test_https_integration'], cwd=ROOT, check=True)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.set_ciphers('ECDHE-RSA-AES128-GCM-SHA256')
    context.load_cert_chain(str(Path(tmp,'server.crt')),str(Path(tmp,'server.key')))
    listener = socket.socket()
    listener.bind(('127.0.0.1',0))
    listener.listen(1)
    listener.settimeout(10)
    port=listener.getsockname()[1]
    errors=[]
    def server():
        try:
            for expected in (b'GET / HTTP/1.0\r\n',
                             b'GET /test/path?x=1 HTTP/1.0\r\n',
                             b'GET /redirect HTTP/1.0\r\n'):
                sock,_=listener.accept()
                sock.settimeout(10)
                with context.wrap_socket(sock,server_side=True) as conn:
                    request=b''
                    while b'\r\n\r\n' not in request:
                        piece=conn.recv(2048)
                        if not piece: raise AssertionError('no HTTP request')
                        request+=piece
                        if len(request)>4096: raise AssertionError('oversized HTTP request')
                    assert expected in request, request
                    assert ('Host: tiny64-test.invalid:%d\r\n' % port).encode() in request, request
                    if b'/redirect' in expected:
                        conn.sendall(b'HTTP/1.1 302 Found\r\n'
                            b'Location: https://tiny64-test.invalid/test/path?x=1\r\n'
                            b'Content-Length: 0\r\n\r\n')
                    elif b'/test/path' in expected:
                        conn.sendall(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n'
                            b'Content-Type: text/plain\r\n\r\n'
                            b'5\r\nHello\r\n7\r\n Tiny64\r\n0\r\n\r\n')
                    else:
                        conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 12\r\n'
                            b'Content-Type: text/plain\r\n\r\nHello Tiny64')
        except Exception as exc:
            errors.append(exc)
        finally:
            listener.close()
    thread=threading.Thread(target=server,daemon=True)
    thread.start()
    subprocess.run([str(OUT/'test_https_integration'),str(port)], cwd=ROOT,
                   timeout=12,check=True)
    thread.join(timeout=2)
    if errors: raise errors[0]
