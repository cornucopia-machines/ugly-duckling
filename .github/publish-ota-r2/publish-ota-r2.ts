#!/usr/bin/env npx tsx
/**
 * Publish OTA binaries to Cloudflare R2, prune old builds, and manage manifests.
 *
 * - Uploads *.bin and *.elf from an artifacts directory to R2: <channel>/<version>/...
 * - <version> comes from `git describe --tags --always`.
 * - Writes per-build manifest.json (with sha256), updates latest.json, and manages all-manifests.json.
 * - Prunes old builds keeping only the most recent N builds per channel.
 *
 * Requirements: Node.js 18+, `git` on PATH.
 */

import { execSync } from "child_process";
import { createHash } from "crypto";
import { existsSync, readdirSync, readFileSync, statSync } from "fs";
import { basename, join } from "path";
import { program } from "commander";
import {
  S3Client,
  GetObjectCommand,
  PutObjectCommand,
  DeleteObjectCommand,
  ListObjectsV2Command,
} from "@aws-sdk/client-s3";

// Types
type Channel = "snapshots" | "releases";

interface FileEntry {
  name: string;
  url: string;
  sha256: string;
}

interface BuildManifest {
  commit: string;
  version: string;
  date: string;
  files: FileEntry[];
  branch?: string;
}

interface Config {
  artifactsDir: string;
  repoRoot: string;
  baseUrl: string;
  branch?: string;
  commitSha?: string;
  versionOverride?: string;
  maxBuildsPerChannel: number;
  r2AccountId: string;
  r2AccessKeyId: string;
  r2SecretAccessKey: string;
  r2BucketName: string;
}

// Utilities
function run(cmd: string, cwd?: string): string {
  try {
    return execSync(cmd, { cwd, encoding: "utf-8" }).trim();
  } catch (error) {
    const err = error as { stderr?: string; message?: string };
    throw new Error(err.stderr?.trim() || err.message || "Command failed");
  }
}

function gitRevParse(repoRoot: string, ref = "HEAD"): string {
  return run(`git -C "${repoRoot}" rev-parse ${ref}`);
}

function gitDescribe(repoRoot: string, commitSha: string): string {
  return run(
    `git -C "${repoRoot}" describe --tags --always ${commitSha}`,
  );
}

function sha256File(filePath: string): string {
  const content = readFileSync(filePath);
  return createHash("sha256").update(content).digest("hex");
}

function findFilesByExtensions(dir: string, extensions: string[]): string[] {
  const results: string[] = [];

  function walk(currentDir: string) {
    const entries = readdirSync(currentDir, { withFileTypes: true });
    for (const entry of entries) {
      const fullPath = join(currentDir, entry.name);
      if (entry.isDirectory()) {
        walk(fullPath);
      } else if (extensions.some((ext) => entry.name.endsWith(ext))) {
        results.push(fullPath);
      }
    }
  }

  if (existsSync(dir) && statSync(dir).isDirectory()) {
    walk(dir);
  }
  return results;
}

function nowUtcIso(): string {
  return new Date().toISOString().replace(/\.\d{3}Z$/, "Z");
}

function isSnapshot(version: string): boolean {
  // A snapshot version ends with a commit hash like "-ga728583"
  return /-g[0-9a-f]+$/.test(version);
}

function getChannel(version: string): Channel {
  return isSnapshot(version) ? "snapshots" : "releases";
}

// R2 Client
function createR2Client(config: Config): S3Client {
  return new S3Client({
    region: "auto",
    endpoint: `https://${config.r2AccountId}.r2.cloudflarestorage.com`,
    credentials: {
      accessKeyId: config.r2AccessKeyId,
      secretAccessKey: config.r2SecretAccessKey,
    },
  });
}

async function r2Get(
  client: S3Client,
  bucket: string,
  key: string,
): Promise<string | null> {
  try {
    const response = await client.send(
      new GetObjectCommand({ Bucket: bucket, Key: key }),
    );
    return (await response.Body?.transformToString()) ?? null;
  } catch (error) {
    const err = error as { name?: string };
    if (err.name === "NoSuchKey") {
      return null;
    }
    throw error;
  }
}

async function r2Put(
  client: S3Client,
  bucket: string,
  key: string,
  body: Buffer | string,
  contentType: string,
): Promise<void> {
  await client.send(
    new PutObjectCommand({
      Bucket: bucket,
      Key: key,
      Body: body,
      ContentType: contentType,
    }),
  );
}

async function r2Delete(
  client: S3Client,
  bucket: string,
  key: string,
): Promise<void> {
  await client.send(new DeleteObjectCommand({ Bucket: bucket, Key: key }));
}

async function r2ListPrefix(
  client: S3Client,
  bucket: string,
  prefix: string,
): Promise<string[]> {
  const keys: string[] = [];
  let continuationToken: string | undefined;

  do {
    const response = await client.send(
      new ListObjectsV2Command({
        Bucket: bucket,
        Prefix: prefix,
        ContinuationToken: continuationToken,
      }),
    );
    for (const obj of response.Contents ?? []) {
      if (obj.Key) {
        keys.push(obj.Key);
      }
    }
    continuationToken = response.NextContinuationToken;
  } while (continuationToken);

  return keys;
}

// Main logic
async function downloadAllManifests(
  client: S3Client,
  bucket: string,
  channel: string,
): Promise<BuildManifest[]> {
  const key = `${channel}/all-manifests.json`;
  const content = await r2Get(client, bucket, key);
  if (!content) {
    console.log(`No existing ${key} found, starting fresh.`);
    return [];
  }
  try {
    return JSON.parse(content) as BuildManifest[];
  } catch {
    console.warn(`Failed to parse ${key}, starting fresh.`);
    return [];
  }
}

async function uploadAllManifests(
  client: S3Client,
  bucket: string,
  channel: string,
  manifests: BuildManifest[],
): Promise<void> {
  const key = `${channel}/all-manifests.json`;
  const content = JSON.stringify(manifests, null, 2) + "\n";
  await r2Put(client, bucket, key, content, "application/json");
  console.log(`Uploaded ${key}`);
}

async function uploadLatestManifest(
  client: S3Client,
  bucket: string,
  channel: string,
  manifest: BuildManifest,
): Promise<void> {
  const key = `${channel}/latest.json`;
  const content = JSON.stringify(manifest, null, 2) + "\n";
  await r2Put(client, bucket, key, content, "application/json");
  console.log(`Uploaded ${key}`);
}

async function uploadBuildManifest(
  client: S3Client,
  bucket: string,
  channel: string,
  version: string,
  manifest: BuildManifest,
): Promise<void> {
  const key = `${channel}/${version}/manifest.json`;
  const content = JSON.stringify(manifest, null, 2) + "\n";
  await r2Put(client, bucket, key, content, "application/json");
  console.log(`Uploaded ${key}`);
}

async function uploadArtifact(
  client: S3Client,
  bucket: string,
  channel: string,
  version: string,
  filePath: string,
): Promise<void> {
  const fileName = basename(filePath);
  const key = `${channel}/${version}/${fileName}`;
  const content = readFileSync(filePath);

  const contentType = fileName.endsWith(".bin")
    ? "application/octet-stream"
    : fileName.endsWith(".elf")
      ? "application/x-elf"
      : "application/octet-stream";

  await r2Put(client, bucket, key, content, contentType);
  console.log(`Uploaded ${key}`);
}

async function deleteVersionFromR2(
  client: S3Client,
  bucket: string,
  channel: string,
  version: string,
): Promise<void> {
  const prefix = `${channel}/${version}/`;
  const keys = await r2ListPrefix(client, bucket, prefix);

  for (const key of keys) {
    await r2Delete(client, bucket, key);
    console.log(`Deleted ${key}`);
  }
}

async function pruneOldBuilds(
  client: S3Client,
  bucket: string,
  channel: string,
  manifests: BuildManifest[],
  maxBuilds: number,
): Promise<BuildManifest[]> {
  if (manifests.length <= maxBuilds) {
    return manifests;
  }

  // Sort by date (newest first)
  const sorted = [...manifests].sort(
    (a, b) => new Date(b.date).getTime() - new Date(a.date).getTime(),
  );

  const keep = sorted.slice(0, maxBuilds);
  const remove = sorted.slice(maxBuilds);

  for (const manifest of remove) {
    console.log(`Pruning old version: ${manifest.version}`);
    await deleteVersionFromR2(client, bucket, channel, manifest.version);
  }

  return keep;
}

async function main() {
  program
    .requiredOption("--artifacts <path>", "Path to build artifacts to publish")
    .requiredOption("--repo-root <path>", "Source repo root")
    .requiredOption(
      "--base-url <url>",
      "Public R2 base URL (e.g., https://ota.example.com)",
    )
    .option("--branch <name>", "Branch the artifact was published from")
    .option("--commit-sha <sha>", "Commit SHA to describe (default: HEAD)")
    .option("--version-override <version>", "Override version string")
    .option(
      "--max-builds <n>",
      "Maximum builds to keep per channel",
      (v) => Number(v),
      30,
    )
    .requiredOption(
      "--r2-account-id <id>",
      "Cloudflare R2 account ID",
      process.env.R2_ACCOUNT_ID,
    )
    .requiredOption(
      "--r2-access-key-id <id>",
      "Cloudflare R2 access key ID",
      process.env.R2_ACCESS_KEY_ID,
    )
    .requiredOption(
      "--r2-secret-access-key <key>",
      "Cloudflare R2 secret access key",
      process.env.R2_SECRET_ACCESS_KEY,
    )
    .requiredOption(
      "--r2-bucket-name <name>",
      "Cloudflare R2 bucket name",
      process.env.R2_BUCKET_NAME,
    )
    .parse();

  const opts = program.opts();
  const config: Config = {
    artifactsDir: opts.artifacts,
    repoRoot: opts.repoRoot,
    baseUrl: opts.baseUrl.replace(/\/$/, ""),
    branch: opts.branch,
    commitSha: opts.commitSha,
    versionOverride: opts.versionOverride,
    maxBuildsPerChannel: opts.maxBuilds,
    r2AccountId: opts.r2AccountId,
    r2AccessKeyId: opts.r2AccessKeyId,
    r2SecretAccessKey: opts.r2SecretAccessKey,
    r2BucketName: opts.r2BucketName,
  };

  // Validate R2 credentials
  if (
    !config.r2AccountId ||
    !config.r2AccessKeyId ||
    !config.r2SecretAccessKey ||
    !config.r2BucketName
  ) {
    console.error(
      "ERROR: Missing R2 credentials. Set via --r2-* options or environment variables.",
    );
    process.exit(2);
  }

  // Debug: print R2 endpoint
  console.log(
    `R2 endpoint: https://${config.r2AccountId}.r2.cloudflarestorage.com`,
  );
  console.log(`R2 bucket: ${config.r2BucketName}`);

  // Resolve commit SHA
  const commitSha = config.commitSha || gitRevParse(config.repoRoot);
  console.log(`Commit SHA: ${commitSha}`);

  // Resolve version
  const version =
    config.versionOverride || gitDescribe(config.repoRoot, commitSha);
  console.log(`Version: ${version}`);

  // Determine channel
  const channel = getChannel(version);
  console.log(`Channel: ${channel}`);

  // Find artifacts
  const artifactFiles = findFilesByExtensions(config.artifactsDir, [
    ".bin",
    ".elf",
  ]);
  if (artifactFiles.length === 0) {
    console.error(
      `ERROR: No .bin or .elf files found under ${config.artifactsDir}`,
    );
    process.exit(1);
  }
  console.log(`Found ${artifactFiles.length} artifact files`);

  // Create R2 client
  const client = createR2Client(config);

  // Download existing all-manifests.json
  const allManifests = await downloadAllManifests(
    client,
    config.r2BucketName,
    channel,
  );
  console.log(`Existing builds in ${channel}: ${allManifests.length}`);

  // Check if this version already exists
  const existingIndex = allManifests.findIndex((m) => m.version === version);
  if (existingIndex !== -1) {
    console.log(`Version ${version} already exists, removing old entry`);
    // Delete old files from R2
    await deleteVersionFromR2(client, config.r2BucketName, channel, version);
    allManifests.splice(existingIndex, 1);
  }

  // Upload artifacts and build file entries
  const files: FileEntry[] = [];
  for (const filePath of artifactFiles.sort()) {
    const fileName = basename(filePath);
    const sha256 = sha256File(filePath);
    const url = `${config.baseUrl}/${channel}/${version}/${fileName}`;

    await uploadArtifact(
      client,
      config.r2BucketName,
      channel,
      version,
      filePath,
    );

    files.push({ name: fileName, url, sha256 });
  }

  // Create build manifest
  const buildManifest: BuildManifest = {
    commit: commitSha,
    version,
    date: nowUtcIso(),
    files,
  };
  if (config.branch) {
    buildManifest.branch = config.branch;
  }

  // Upload build manifest
  await uploadBuildManifest(
    client,
    config.r2BucketName,
    channel,
    version,
    buildManifest,
  );

  // Add to all-manifests
  allManifests.unshift(buildManifest);

  // Prune old builds
  const prunedManifests = await pruneOldBuilds(
    client,
    config.r2BucketName,
    channel,
    allManifests,
    config.maxBuildsPerChannel,
  );

  // Upload updated all-manifests.json
  await uploadAllManifests(
    client,
    config.r2BucketName,
    channel,
    prunedManifests,
  );

  // Upload latest.json
  await uploadLatestManifest(
    client,
    config.r2BucketName,
    channel,
    buildManifest,
  );

  console.log("Done.");
}

main().catch((error) => {
  console.error("ERROR:", error.stack || error);
  process.exit(1);
});
