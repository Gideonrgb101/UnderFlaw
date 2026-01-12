
import struct
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
import numpy as np
import os
import time

# --- CONFIGURATION ---
DATA_FILE = 'training_data_sparse.bin'
OUTPUT_NET = 'assets/chess_net.nnue'
BATCH_SIZE = 1024 # Smaller batch for CPU
EPOCHS = 10
LEARNING_RATE = 0.001
INPUT_DIM = 40960
HIDDEN_DIM = 256
QA = 127 

# --- DATASET ---
class SparseChessDataset(Dataset):
    def __init__(self, filename):
        self.samples = []
        print(f"Loading sparse data from {filename}...")
        try:
            with open(filename, 'rb') as f:
                while True:
                    # Read Target (float)
                    data = f.read(4)
                    if len(data) < 4: break # EOF
                    target = struct.unpack('f', data)[0]
                    
                    try:
                        # Read White Indices
                        data = f.read(4)
                        if len(data) < 4: break
                        len_w = struct.unpack('i', data)[0]
                        w_indices = []
                        for _ in range(len_w): 
                            data = f.read(4)
                            if len(data) < 4: raise EOFError
                            w_indices.append(struct.unpack('i', data)[0])
                            
                        # Read Black Indices
                        data = f.read(4)
                        if len(data) < 4: raise EOFError
                        len_b = struct.unpack('i', data)[0]
                        b_indices = []
                        for _ in range(len_b): 
                            data = f.read(4)
                            if len(data) < 4: raise EOFError
                            b_indices.append(struct.unpack('i', data)[0])
                            
                        self.samples.append({'target': target, 'white': w_indices, 'black': b_indices})
                    except EOFError:
                        print("Warning: Truncated record found at end of file. Ignoring.")
                        break
        except FileNotFoundError:
            print(f"Error: File {filename} not found!")
            exit(1)
        print(f"Loaded {len(self.samples)} positions.")

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        sample = self.samples[idx]
        input_w = torch.zeros(INPUT_DIM)
        input_b = torch.zeros(INPUT_DIM)
        for i in sample['white']: 
            if i < INPUT_DIM: input_w[int(i)] = 1.0
        for i in sample['black']: 
            if i < INPUT_DIM: input_b[int(i)] = 1.0
        features = torch.cat([input_w, input_b])
        # Target scaling: (0.5 -> 0 cp, 1.0 -> ~200cp). 
        # Approx: (target - 0.5) * 4.0 covers reasonable range
        real_target = (sample['target'] - 0.5) * 4.0
        return features, torch.tensor([real_target], dtype=torch.float32)

# --- MODEL ---
class HalfKPModel(nn.Module):
    def __init__(self):
        super(HalfKPModel, self).__init__()
        self.feature_layer = nn.Linear(INPUT_DIM, HIDDEN_DIM)
        self.output_layer = nn.Linear(2 * HIDDEN_DIM, 1)

    def forward(self, features):
        w_feat = features[:, :INPUT_DIM]
        b_feat = features[:, INPUT_DIM:]
        w_acc = torch.clamp(self.feature_layer(w_feat), 0, 1)
        b_acc = torch.clamp(self.feature_layer(b_feat), 0, 1)
        combined = torch.cat([w_acc, b_acc], dim=1)
        return self.output_layer(combined)

    def export_to_engine(self, filename):
        print(f"Exporting to {filename}...")
        os.makedirs(os.path.dirname(filename), exist_ok=True)
        with open(filename, "wb") as f:
            def write_layer(tensor, scale):
                valid = (tensor.data * scale).clamp(-32768, 32767).to(torch.int16).cpu().numpy()
                valid.tofile(f)
            write_layer(self.feature_layer.weight, QA)
            write_layer(self.feature_layer.bias, QA)
            write_layer(self.output_layer.weight, QA)
            write_layer(self.output_layer.bias, QA)
        print(f"Success! {filename} saved.")

# --- CHECKPOINT UTILS ---
CHECKPOINT_FILE = 'checkpoint.pt'

def save_checkpoint(model, optimizer, epoch, loss, filename):
    print(f"Saving training state to {filename}...")
    torch.save({
        'epoch': epoch,
        'model_state_dict': model.state_dict(),
        'optimizer_state_dict': optimizer.state_dict(),
        'loss': loss,
    }, filename)

def load_checkpoint(model, optimizer, filename):
    if os.path.isfile(filename):
        print(f"Loading checkpoint '{filename}'...")
        checkpoint = torch.load(filename)
        model.load_state_dict(checkpoint['model_state_dict'])
        optimizer.load_state_dict(checkpoint['optimizer_state_dict'])
        epoch = checkpoint['epoch']
        loss = checkpoint['loss']
        print(f"Resumed from epoch {epoch+1} with loss {loss:.5f}")
        return epoch + 1
    return 0

# --- TRAIN ---
def train():
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Using device: {device}")
    
    dataset = SparseChessDataset(DATA_FILE)
    if len(dataset) == 0: return

    dataloader = DataLoader(dataset, batch_size=BATCH_SIZE, shuffle=True)
    model = HalfKPModel().to(device)
    optimizer = optim.Adam(model.parameters(), lr=LEARNING_RATE)
    criterion = nn.MSELoss()
    
    # Try to resume
    start_epoch = load_checkpoint(model, optimizer, CHECKPOINT_FILE)

    print(f"Starting training for {EPOCHS} epochs (from {start_epoch})...")
    start_total = time.time()
    
    for epoch in range(start_epoch, start_epoch + EPOCHS):
        model.train()
        total_loss = 0
        steps = 0
        
        for inputs, targets in dataloader:
            inputs, targets = inputs.to(device), targets.to(device)
            optimizer.zero_grad()
            outputs = model(inputs)
            loss = criterion(outputs, targets)
            loss.backward()
            optimizer.step()
            total_loss += loss.item()
            steps += 1
            if steps % 10 == 0: print(f".", end="", flush=True)
            
        avg_loss = total_loss/steps
        print(f"\nEpoch {epoch+1}: Loss {avg_loss:.5f}")
        
        # Save checkpoint after each epoch
        save_checkpoint(model, optimizer, epoch, avg_loss, CHECKPOINT_FILE)
        
        # Also update the engine network immediately so user can see progress
        model.export_to_engine(OUTPUT_NET)

    print(f"Training Time: {(time.time()-start_total)/60:.1f} min")

if __name__ == '__main__':
    train()
