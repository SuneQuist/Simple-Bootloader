#!/usr/bin/env python3
"""Create an ephemeral, test-only RSA CA and TLS leaf certificate.
Private keys remain in memory and are never embedded in Tiny64.
Requires host Python cryptography; no network connection.
"""
from pathlib import Path
from datetime import datetime, timezone
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID

root_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
leaf_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
root_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Tiny64 test-only CA')])
leaf_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'tiny64-test.invalid')])
start = datetime(2025, 1, 1, tzinfo=timezone.utc)
end = datetime(2035, 1, 1, tzinfo=timezone.utc)
root = (x509.CertificateBuilder().subject_name(root_name).issuer_name(root_name)
        .public_key(root_key.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(start).not_valid_after(end)
        .add_extension(x509.BasicConstraints(ca=True, path_length=1), critical=True)
        .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                       key_encipherment=False, data_encipherment=False, key_agreement=False,
                       key_cert_sign=True, crl_sign=True, encipher_only=False, decipher_only=False), critical=True)
        .sign(root_key, hashes.SHA256()))
leaf = (x509.CertificateBuilder().subject_name(leaf_name).issuer_name(root_name)
        .public_key(leaf_key.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(start).not_valid_after(end)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.SubjectAlternativeName([x509.DNSName('tiny64-test.invalid')]), critical=False)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                       key_encipherment=True, data_encipherment=False, key_agreement=False,
                       key_cert_sign=False, crl_sign=False, encipher_only=False, decipher_only=False), critical=True)
        .sign(root_key, hashes.SHA256()))


def array(name, data):
    lines = [f'static const unsigned char {name}[] = {{']
    for i in range(0, len(data), 12):
        lines.append('    ' + ', '.join(f'0x{v:02x}' for v in data[i:i+12]) + ',')
    return '\n'.join(lines + ['};'])

numbers = root_key.public_key().public_numbers()
n = numbers.n.to_bytes((numbers.n.bit_length()+7)//8, 'big')
e = numbers.e.to_bytes((numbers.e.bit_length()+7)//8, 'big')
header = '\n'.join([
    '/* Test-only ephemeral fixture, generated for this host test. */',
    array('test_root_dn', root.subject.public_bytes()),
    array('test_root_n', n),
    array('test_root_e', e),
    array('test_leaf_der', leaf.public_bytes(serialization.Encoding.DER)),
]) + '\n'
Path('compiled/generated_x509_fixture.h').write_text(header)
