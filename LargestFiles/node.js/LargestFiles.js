const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const readline = require('readline');

// Function to reliably enumerate drives in Windows
function enumerateDrivesSync() {
    try {
        const output = execSync('wmic logicaldisk get caption,drivetype', { encoding: 'utf-8' });
        const drives = [];
        const lines = output.split('\r\n').filter(line => line.trim() !== '');
        for (let i = 1; i < lines.length; i++) {
            const line = lines[i].trim();
            const parts = line.split(/\s+/);
            if (parts.length < 2) continue;
            const caption = parts[0];
            const drivetype = parts[1];
            // Drive types: 2 (Removable), 3 (Fixed)
            if (drivetype === '2' || drivetype === '3') {
                drives.push(`${caption}\\`);
            }
        }
        console.log('[Drive Scan]');
        drives.forEach(drive => {
            console.log(`Including drive: ${drive}`);
        });
        return drives;
    } catch (error) {
        console.error('Error enumerating drives:', error);
        return [];
    }
}

// Recursive function to scan directories and files
function scanDirectory(dirPath, files = []) {
    try {
        const entries = fs.readdirSync(dirPath, { withFileTypes: true });

        for (const entry of entries) {
            if (entry.name === '.' || entry.name === '..') continue; // Skip . and ..

            const fullPath = path.join(dirPath, entry.name);

            try {
                if (entry.isDirectory()) {
                    // Recursively scan subdirectories
                    scanDirectory(fullPath, files);
                } else if (entry.isFile()) {
                    const stats = fs.statSync(fullPath);
                    // Removed hidden/system check to avoid skipping files on Windows.
                    files.push({
                        path: fullPath,
                        size: stats.size
                    });
                }
            } catch (innerErr) {
                // Skip files/directories that cause errors (e.g., permissions)
                continue;
            }
        }
    } catch (outerErr) {
        // Skip directories that cannot be read
    }
    return files;
}

async function main() {
    console.log("File Scanner");
    console.log("----------------------------------------");

    const outfile = 'largest_files.txt';
    fs.writeFileSync(outfile, ''); // Clear or create the output file

    const drives = enumerateDrivesSync();
    if (drives.length === 0) {
        console.error('No suitable drives found!');
        return;
    }

    for (const drive of drives) {
        console.log(`\nProcessing ${drive}`);
        const startTime = Date.now();
        const files = scanDirectory(drive);
        const elapsed = (Date.now() - startTime) / 1000;
        console.log(`Scanned ${drive} in ${elapsed.toFixed(1)} seconds`);
        console.log(`Found ${files.length} files`);

        if (files.length === 0) continue;

        // Sort files by size (largest first)
        files.sort((a, b) => b.size - a.size);

        // Get the top 100 largest files
        const topFiles = files.slice(0, 100);

        const driveDisplay = drive.substring(0, 2);
        const content = [
            `Largest files on ${driveDisplay}:`,
            ...topFiles.map(file => {
                const mb = file.size / (1024 * 1024);
                return `${file.path}: ${mb.toFixed(2)} MB`;
            }),
            '', // extra blank line to separate sections
            ''
        ].join('\n');

        fs.appendFileSync(outfile, content);
    }

    console.log(`\nScan complete. Results saved to ${outfile}`);

    const rl = readline.createInterface({
        input: process.stdin,
        output: process.stdout
    });

    await new Promise(resolve => {
        rl.question('Press Enter to exit...', () => {
            rl.close();
            resolve();
        });
    });
}

main().catch(console.error);
