#!/usr/bin/env python3
"""
quantize.py - Offline quantization for TinyStories model
Generates int8 weight files for Gemmini accelerated inference

Usage:
    python3 quantize.py stories15M.bin stories15M_int8.bin

Output file format:
    - Config (28 bytes, same as original)
    - QuantConfig: n_weight_scales, n_act_scales (8 bytes)
    - Weight scales (float32 array)
    - Activation scales (float32 array)
    - Int8 weight data
"""

import struct
import numpy as np
import sys
import os

# Model config structure
CONFIG_SIZE = 28  # 7 int32 values

def read_config(f):
    """Read model config"""
    data = f.read(CONFIG_SIZE)
    dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len = struct.unpack('7i', data)
    
    # Negative vocab_size indicates unshared weights
    shared_weights = vocab_size > 0
    vocab_size = abs(vocab_size)
    
    return {
        'dim': dim,
        'hidden_dim': hidden_dim,
        'n_layers': n_layers,
        'n_heads': n_heads,
        'n_kv_heads': n_kv_heads,
        'vocab_size': vocab_size,
        'seq_len': seq_len,
        'shared_weights': shared_weights
    }

def quantize_tensor(tensor, name=""):
    """Symmetric quantization: scale = max(abs(tensor)) / 127"""
    max_abs = np.max(np.abs(tensor))
    if max_abs < 1e-10:
        scale = 1.0
    else:
        scale = max_abs / 127.0
    
    # Quantize to int8
    quantized = np.clip(np.round(tensor / scale), -128, 127).astype(np.int8)
    
    # Statistics
    if name:
        error = np.mean(np.abs(tensor - quantized.astype(np.float32) * scale))
        print(f"  {name}: shape={tensor.shape}, max={max_abs:.4f}, scale={scale:.6f}, quant_err={error:.6f}")
    
    return quantized, scale

def calibrate_activations(config, weights, n_samples=10):
    """
    Calibrate activation value ranges
    Run a few samples and record the max absolute value at each activation point
    """
    print("\n=== Activation Calibration ===")
    
    dim = config['dim']
    hidden_dim = config['hidden_dim']
    n_layers = config['n_layers']
    n_heads = config['n_heads']
    n_kv_heads = config['n_kv_heads']
    kv_dim = dim * n_kv_heads // n_heads
    head_size = dim // n_heads
    seq_len = config['seq_len']
    vocab_size = config['vocab_size']
    
    # Activation range recorder
    # Activation points to record per layer:
    # - xb_after_rmsnorm (before attention)
    # - q, k, v (projection outputs, but range changes after RoPE)
    # - xb_after_attn (attention output)
    # - xb_after_ffn_rmsnorm
    # - hb, hb2 (FFN intermediate)
    # - hb_after_silu (after SiLU activation)
    # - x_final (after final layer)
    
    act_ranges = {
        'xb': np.zeros(n_layers),           # After RMSNorm (attention input)
        'q': np.zeros(n_layers),            # Q projection output
        'k': np.zeros(n_layers),            # K projection output
        'v': np.zeros(n_layers),            # V projection output
        'xb_attn': np.zeros(n_layers),      # Attention output
        'xb_ffn': np.zeros(n_layers),       # After FFN RMSNorm
        'hb': np.zeros(n_layers),           # W1 output
        'hb2': np.zeros(n_layers),          # W3 output
        'hb_silu': np.zeros(n_layers),      # After SiLU
        'x_final': 0.0,                     # Final output
        'logits': 0.0,                      # Logits
    }
    
    def rmsnorm(x, weight):
        ss = np.sum(x ** 2) / len(x)
        ss = 1.0 / np.sqrt(ss + 1e-5)
        return weight * (ss * x)
    
    def softmax(x):
        x = x - np.max(x)
        e = np.exp(x)
        return e / np.sum(e)
    
    def matmul(x, w):
        # x: (n,), w: (d, n), out: (d,)
        return w @ x
    
    # Simple token sequences for calibration
    # Using some common token combinations
    calibration_tokens = [
        [1, 450, 2501, 263, 931],   # "Once upon a time"
        [1, 13, 851, 338, 263],     # "There was a"
        [1, 530, 4098, 29892, 278], # "One day, the"
        [1, 450, 2217, 7826, 471],  # "The little girl was"
        [1, 940, 471, 263, 2919],   # "It was a beautiful"
    ]
    
    for sample_idx, tokens in enumerate(calibration_tokens[:n_samples]):
        print(f"  Calibration sample {sample_idx + 1}/{min(n_samples, len(calibration_tokens))}")
        
        # Initialize KV cache
        key_cache = np.zeros((n_layers, seq_len, kv_dim), dtype=np.float32)
        value_cache = np.zeros((n_layers, seq_len, kv_dim), dtype=np.float32)
        
        for pos, token in enumerate(tokens):
            if token >= vocab_size:
                token = token % vocab_size
                
            # Token embedding
            x = weights['token_embedding_table'][token].copy()
            
            for l in range(n_layers):
                # Attention RMSNorm
                xb = rmsnorm(x, weights['rms_att_weight'][l])
                act_ranges['xb'][l] = max(act_ranges['xb'][l], np.max(np.abs(xb)))
                
                # QKV projection
                q = matmul(xb, weights['wq'][l])
                k = matmul(xb, weights['wk'][l])
                v = matmul(xb, weights['wv'][l])
                
                act_ranges['q'][l] = max(act_ranges['q'][l], np.max(np.abs(q)))
                act_ranges['k'][l] = max(act_ranges['k'][l], np.max(np.abs(k)))
                act_ranges['v'][l] = max(act_ranges['v'][l], np.max(np.abs(v)))
                
                # RoPE
                for i in range(0, dim, 2):
                    head_dim = i % head_size
                    freq = 1.0 / (10000.0 ** (head_dim / head_size))
                    val = pos * freq
                    fcr, fci = np.cos(val), np.sin(val)
                    
                    v0, v1 = q[i], q[i+1]
                    q[i] = v0 * fcr - v1 * fci
                    q[i+1] = v0 * fci + v1 * fcr
                    
                    if i < kv_dim:
                        v0, v1 = k[i], k[i+1]
                        k[i] = v0 * fcr - v1 * fci
                        k[i+1] = v0 * fci + v1 * fcr
                
                # KV cache
                key_cache[l, pos, :] = k
                value_cache[l, pos, :] = v
                
                # Multi-head attention
                xb_out = np.zeros(dim, dtype=np.float32)
                kv_mul = n_heads // n_kv_heads
                
                for h in range(n_heads):
                    q_h = q[h * head_size:(h+1) * head_size]
                    
                    # Attention scores
                    att = np.zeros(pos + 1)
                    for t in range(pos + 1):
                        k_h = key_cache[l, t, (h // kv_mul) * head_size:((h // kv_mul) + 1) * head_size]
                        att[t] = np.dot(q_h, k_h) / np.sqrt(head_size)
                    
                    att = softmax(att)
                    
                    # Weighted sum of values
                    for t in range(pos + 1):
                        v_h = value_cache[l, t, (h // kv_mul) * head_size:((h // kv_mul) + 1) * head_size]
                        xb_out[h * head_size:(h+1) * head_size] += att[t] * v_h
                
                act_ranges['xb_attn'][l] = max(act_ranges['xb_attn'][l], np.max(np.abs(xb_out)))
                
                # Output projection
                xb2 = matmul(xb_out, weights['wo'][l])
                
                # Residual connection
                x = x + xb2
                
                # FFN RMSNorm
                xb = rmsnorm(x, weights['rms_ffn_weight'][l])
                act_ranges['xb_ffn'][l] = max(act_ranges['xb_ffn'][l], np.max(np.abs(xb)))
                
                # FFN
                hb = matmul(xb, weights['w1'][l])
                hb2 = matmul(xb, weights['w3'][l])
                
                act_ranges['hb'][l] = max(act_ranges['hb'][l], np.max(np.abs(hb)))
                act_ranges['hb2'][l] = max(act_ranges['hb2'][l], np.max(np.abs(hb2)))
                
                # SiLU
                hb_silu = hb * (1.0 / (1.0 + np.exp(-hb))) * hb2
                act_ranges['hb_silu'][l] = max(act_ranges['hb_silu'][l], np.max(np.abs(hb_silu)))
                
                # W2 projection
                xb = matmul(hb_silu, weights['w2'][l])
                
                # Residual connection
                x = x + xb
            
            # Final RMSNorm
            x = rmsnorm(x, weights['rms_final_weight'])
            act_ranges['x_final'] = max(act_ranges['x_final'], np.max(np.abs(x)))
            
            # Logits
            logits = matmul(x, weights['wcls'])
            act_ranges['logits'] = max(act_ranges['logits'], np.max(np.abs(logits)))
    
    # Convert to scale (add some margin, e.g. 1.2x)
    margin = 1.2
    act_scales = {}
    
    print("\nActivation ranges and scales:")
    for key, val in act_ranges.items():
        if isinstance(val, np.ndarray):
            # One scale per layer
            scales = []
            for l in range(n_layers):
                max_val = val[l] * margin
                scale = max_val / 127.0 if max_val > 1e-10 else 1.0
                scales.append(scale)
                print(f"  {key}[{l}]: max={val[l]:.4f}, scale={scale:.6f}")
            act_scales[key] = np.array(scales, dtype=np.float32)
        else:
            max_val = val * margin
            scale = max_val / 127.0 if max_val > 1e-10 else 1.0
            print(f"  {key}: max={val:.4f}, scale={scale:.6f}")
            act_scales[key] = np.array([scale], dtype=np.float32)
    
    return act_scales

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 quantize.py <input.bin> <output_int8.bin>")
        print("Example: python3 quantize.py stories15M.bin stories15M_int8.bin")
        sys.exit(1)
    
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    
    print(f"=== Quantizing {input_path} ===\n")
    
    # Read model
    with open(input_path, 'rb') as f:
        config = read_config(f)
        
        print("Model config:")
        for k, v in config.items():
            print(f"  {k}: {v}")
        
        # Calculate weight sizes
        dim = config['dim']
        hidden_dim = config['hidden_dim']
        n_layers = config['n_layers']
        n_heads = config['n_heads']
        n_kv_heads = config['n_kv_heads']
        vocab_size = config['vocab_size']
        seq_len = config['seq_len']
        kv_dim = dim * n_kv_heads // n_heads
        head_size = dim // n_heads
        
        # Read weights
        print("\nReading weights...")
        weights = {}
        
        weights['token_embedding_table'] = np.frombuffer(
            f.read(vocab_size * dim * 4), dtype=np.float32
        ).reshape(vocab_size, dim)
        
        weights['rms_att_weight'] = np.frombuffer(
            f.read(n_layers * dim * 4), dtype=np.float32
        ).reshape(n_layers, dim)
        
        weights['wq'] = np.frombuffer(
            f.read(n_layers * dim * dim * 4), dtype=np.float32
        ).reshape(n_layers, dim, dim)
        
        weights['wk'] = np.frombuffer(
            f.read(n_layers * dim * kv_dim * 4), dtype=np.float32
        ).reshape(n_layers, kv_dim, dim)
        
        weights['wv'] = np.frombuffer(
            f.read(n_layers * dim * kv_dim * 4), dtype=np.float32
        ).reshape(n_layers, kv_dim, dim)
        
        weights['wo'] = np.frombuffer(
            f.read(n_layers * dim * dim * 4), dtype=np.float32
        ).reshape(n_layers, dim, dim)
        
        weights['rms_ffn_weight'] = np.frombuffer(
            f.read(n_layers * dim * 4), dtype=np.float32
        ).reshape(n_layers, dim)
        
        weights['w1'] = np.frombuffer(
            f.read(n_layers * dim * hidden_dim * 4), dtype=np.float32
        ).reshape(n_layers, hidden_dim, dim)
        
        weights['w2'] = np.frombuffer(
            f.read(n_layers * hidden_dim * dim * 4), dtype=np.float32
        ).reshape(n_layers, dim, hidden_dim)
        
        weights['w3'] = np.frombuffer(
            f.read(n_layers * dim * hidden_dim * 4), dtype=np.float32
        ).reshape(n_layers, hidden_dim, dim)
        
        weights['rms_final_weight'] = np.frombuffer(
            f.read(dim * 4), dtype=np.float32
        )
        
        # Skip freq_cis (we compute on the fly)
        f.read(seq_len * head_size // 2 * 4)  # freq_cis_real
        f.read(seq_len * head_size // 2 * 4)  # freq_cis_imag
        
        # wcls (classifier) - may share with embedding
        if config['shared_weights']:
            weights['wcls'] = weights['token_embedding_table']
        else:
            weights['wcls'] = np.frombuffer(
                f.read(vocab_size * dim * 4), dtype=np.float32
            ).reshape(vocab_size, dim)
    
    # Calibrate activations
    act_scales = calibrate_activations(config, weights, n_samples=5)
    
    # Quantize weights
    print("\n=== Quantizing Weights ===")
    quantized_weights = {}
    weight_scales = {}
    
    # Token embedding (keep float, since it's a lookup)
    quantized_weights['token_embedding_table'] = weights['token_embedding_table']
    
    # RMS weights (keep float, since it's element-wise multiplication)
    quantized_weights['rms_att_weight'] = weights['rms_att_weight']
    quantized_weights['rms_ffn_weight'] = weights['rms_ffn_weight']
    quantized_weights['rms_final_weight'] = weights['rms_final_weight']
    
    # Quantize embedding (for classifier; note that embedding lookup still uses float32)
    print("\nEmbedding (for classifier):")
    q, s = quantize_tensor(weights['token_embedding_table'], "token_embedding")
    quantized_weights['token_embedding_int8'] = q
    weight_scales['embedding'] = s
    
    # Quantize linear layer weights
    for l in range(n_layers):
        print(f"\nLayer {l}:")
        
        q, s = quantize_tensor(weights['wq'][l], f"wq[{l}]")
        quantized_weights[f'wq_{l}'] = q
        weight_scales[f'wq_{l}'] = s
        
        q, s = quantize_tensor(weights['wk'][l], f"wk[{l}]")
        quantized_weights[f'wk_{l}'] = q
        weight_scales[f'wk_{l}'] = s
        
        q, s = quantize_tensor(weights['wv'][l], f"wv[{l}]")
        quantized_weights[f'wv_{l}'] = q
        weight_scales[f'wv_{l}'] = s
        
        q, s = quantize_tensor(weights['wo'][l], f"wo[{l}]")
        quantized_weights[f'wo_{l}'] = q
        weight_scales[f'wo_{l}'] = s
        
        q, s = quantize_tensor(weights['w1'][l], f"w1[{l}]")
        quantized_weights[f'w1_{l}'] = q
        weight_scales[f'w1_{l}'] = s
        
        q, s = quantize_tensor(weights['w2'][l], f"w2[{l}]")
        quantized_weights[f'w2_{l}'] = q
        weight_scales[f'w2_{l}'] = s
        
        q, s = quantize_tensor(weights['w3'][l], f"w3[{l}]")
        quantized_weights[f'w3_{l}'] = q
        weight_scales[f'w3_{l}'] = s
    
    # Classifier (if not shared) - no longer needs separate quantization, uses embedding_int8
    # if not config['shared_weights']:
    #     q, s = quantize_tensor(weights['wcls'], "wcls")
    #     quantized_weights['wcls'] = q
    #     weight_scales['wcls'] = s
    
    # Save quantized model
    print(f"\n=== Saving to {output_path} ===")
    
    with open(output_path, 'wb') as f:
        # 1. Config (original format)
        f.write(struct.pack('7i',
            config['dim'],
            config['hidden_dim'],
            config['n_layers'],
            config['n_heads'],
            config['n_kv_heads'],
            -config['vocab_size'] if not config['shared_weights'] else config['vocab_size'],
            config['seq_len']
        ))
        
        # 2. Weight scales (7 per layer: wq, wk, wv, wo, w1, w2, w3) + embedding scale
        n_weight_scales = n_layers * 7 + 1  # +1 for embedding
        
        # 3. Number of activation scales
        # xb, q, k, v, xb_attn, xb_ffn, hb, hb2, hb_silu: n_layers each
        # x_final, logits: 1 each
        n_act_scales = n_layers * 9 + 2
        
        f.write(struct.pack('2i', n_weight_scales, n_act_scales))
        
        # 4. Write weight scales
        for l in range(n_layers):
            for name in ['wq', 'wk', 'wv', 'wo', 'w1', 'w2', 'w3']:
                f.write(struct.pack('f', weight_scales[f'{name}_{l}']))
        
        # Write embedding scale (for classifier)
        f.write(struct.pack('f', weight_scales['embedding']))
        
        # 5. Write activation scales
        for key in ['xb', 'q', 'k', 'v', 'xb_attn', 'xb_ffn', 'hb', 'hb2', 'hb_silu']:
            for l in range(n_layers):
                f.write(struct.pack('f', act_scales[key][l]))
        f.write(struct.pack('f', act_scales['x_final'][0]))
        f.write(struct.pack('f', act_scales['logits'][0]))
        
        # 6. Token embedding (float32)
        f.write(quantized_weights['token_embedding_table'].tobytes())
        
        # 7. RMS weights (float32)
        f.write(quantized_weights['rms_att_weight'].tobytes())
        f.write(quantized_weights['rms_ffn_weight'].tobytes())
        f.write(quantized_weights['rms_final_weight'].tobytes())
        
        # 8. Quantized weights (int8)
        for l in range(n_layers):
            f.write(quantized_weights[f'wq_{l}'].tobytes())
            f.write(quantized_weights[f'wk_{l}'].tobytes())
            f.write(quantized_weights[f'wv_{l}'].tobytes())
            f.write(quantized_weights[f'wo_{l}'].tobytes())
            f.write(quantized_weights[f'w1_{l}'].tobytes())
            f.write(quantized_weights[f'w2_{l}'].tobytes())
            f.write(quantized_weights[f'w3_{l}'].tobytes())
        
        # 9. Quantized embedding (int8, for classifier)
        f.write(quantized_weights['token_embedding_int8'].tobytes())
    
    # File size statistics
    orig_size = os.path.getsize(input_path)
    new_size = os.path.getsize(output_path)
    
    print(f"\nOriginal size: {orig_size / 1024 / 1024:.2f} MB")
    print(f"Quantized size: {new_size / 1024 / 1024:.2f} MB")
    print(f"Compression ratio: {orig_size / new_size:.2f}x")

    print("\nQuantization complete!")

if __name__ == '__main__':
    main()
