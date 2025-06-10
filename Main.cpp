#include <vector>
#include <algorithm>
#include <functional> 
#include <memory>
#include <cstdlib>
#include <cmath>
#include <ctime>

#include <raylib.h>
#include <raymath.h>

// --- UTILS ---
namespace Utils {
	inline static float RandomFloat(float min, float max) {
		return min + static_cast<float>(rand()) / RAND_MAX * (max - min);
	}
}

// --- TRANSFORM, PHYSICS, LIFETIME, RENDERABLE ---
struct TransformA {
	Vector2 position{};
	float rotation{};
};

struct Physics {
	Vector2 velocity{};
	float rotationSpeed{};
};

struct Renderable {
	enum Size { SMALL = 1, MEDIUM = 2, LARGE = 4 } size = SMALL;
};

// --- RENDERER ---
class Renderer {
public:
	static Renderer& Instance() {
		static Renderer inst;
		return inst;
	}

	void Init(int w, int h, const char* title) {
		InitWindow(w, h, title);
		SetTargetFPS(60);
		screenW = w;
		screenH = h;
	}

	void Begin() {
		BeginDrawing();
		ClearBackground(BLACK);
	}

	void End() {
		EndDrawing();
	}

	void DrawPoly(const Vector2& pos, int sides, float radius, float rot) {
		DrawPolyLines(pos, sides, radius, rot, WHITE);
	}

	int Width() const {
		return screenW;
	}

	int Height() const {
		return screenH;
	}

private:
	Renderer() = default;

	int screenW{};
	int screenH{};
};

// --- ASTEROID HIERARCHY ---

class Asteroid {
public:
	Asteroid(int screenW, int screenH) {
		init(screenW, screenH);
	}
	virtual ~Asteroid() = default;

	bool Update(float dt) {
		transform.position = Vector2Add(transform.position, Vector2Scale(physics.velocity, dt));
		transform.rotation += physics.rotationSpeed * dt;
		if (transform.position.x < -GetRadius() || transform.position.x > Renderer::Instance().Width() + GetRadius() ||
			transform.position.y < -GetRadius() || transform.position.y > Renderer::Instance().Height() + GetRadius())
			return false;
		return true;
	}
	virtual void Draw() const = 0;

	Vector2 GetPosition() const {
		return transform.position;
	}

	float constexpr GetRadius() const {
		return 16.f * (float)render.size;
	}

	int GetDamage() const {
		return baseDamage * static_cast<int>(render.size);
	}

	int GetSize() const {
		return static_cast<int>(render.size);
	}

protected:
	void init(int screenW, int screenH) {
		// Choose size
		render.size = static_cast<Renderable::Size>(1 << GetRandomValue(0, 2));

		// Spawn at random edge
		switch (GetRandomValue(0, 3)) {
		case 0:
			transform.position = { Utils::RandomFloat(0, screenW), -GetRadius() };
			break;
		case 1:
			transform.position = { screenW + GetRadius(), Utils::RandomFloat(0, screenH) };
			break;
		case 2:
			transform.position = { Utils::RandomFloat(0, screenW), screenH + GetRadius() };
			break;
		default:
			transform.position = { -GetRadius(), Utils::RandomFloat(0, screenH) };
			break;
		}

		// Aim towards center with jitter
		float maxOff = fminf(screenW, screenH) * 0.1f;
		float ang = Utils::RandomFloat(0, 2 * PI);
		float rad = Utils::RandomFloat(0, maxOff);
		Vector2 center = {
										 screenW * 0.5f + cosf(ang) * rad,
										 screenH * 0.5f + sinf(ang) * rad
		};

		Vector2 dir = Vector2Normalize(Vector2Subtract(center, transform.position));
		physics.velocity = Vector2Scale(dir, Utils::RandomFloat(SPEED_MIN, SPEED_MAX));
		physics.rotationSpeed = Utils::RandomFloat(ROT_MIN, ROT_MAX);

		transform.rotation = Utils::RandomFloat(0, 360);
	}

	TransformA transform;
	Physics    physics;
	Renderable render;

	int baseDamage = 0;
	static constexpr float LIFE = 10.f;
	static constexpr float SPEED_MIN = 125.f;
	static constexpr float SPEED_MAX = 250.f;
	static constexpr float ROT_MIN = 50.f;
	static constexpr float ROT_MAX = 240.f;
};

class TriangleAsteroid : public Asteroid {
public:
	TriangleAsteroid(int w, int h) : Asteroid(w, h) { baseDamage = 5; }
	void Draw() const override {
		Renderer::Instance().DrawPoly(transform.position, 3, GetRadius(), transform.rotation);
	}
};
class SquareAsteroid : public Asteroid {
public:
	SquareAsteroid(int w, int h) : Asteroid(w, h) { baseDamage = 10; }
	void Draw() const override {
		Renderer::Instance().DrawPoly(transform.position, 4, GetRadius(), transform.rotation);
	}
};
class PentagonAsteroid : public Asteroid {
public:
	PentagonAsteroid(int w, int h) : Asteroid(w, h) { baseDamage = 15; }
	void Draw() const override {
		Renderer::Instance().DrawPoly(transform.position, 5, GetRadius(), transform.rotation);
	}
};

// Shape selector
enum class AsteroidShape { TRIANGLE = 3, SQUARE = 4, PENTAGON = 5, RANDOM = 0 };

// Factory
static inline std::unique_ptr<Asteroid> MakeAsteroid(int w, int h, AsteroidShape shape) {
	switch (shape) {
	case AsteroidShape::TRIANGLE:
		return std::make_unique<TriangleAsteroid>(w, h);
	case AsteroidShape::SQUARE:
		return std::make_unique<SquareAsteroid>(w, h);
	case AsteroidShape::PENTAGON:
		return std::make_unique<PentagonAsteroid>(w, h);
	default: {
		return MakeAsteroid(w, h, static_cast<AsteroidShape>(3 + GetRandomValue(0, 2)));
	}
	}
}

// --- PROJECTILE HIERARCHY ---
enum class WeaponType { LASER, BULLET, GRENADE, TRISHOT, COUNT};
class Projectile {
public:
	Projectile(Vector2 pos, Vector2 vel, int dmg, WeaponType wt)
	{
		transform.position = pos;
		physics.velocity = vel;
		baseDamage = dmg;
		type = wt;
		explodeTimer = 2.0f; //*-*
		isGrenadeActive = (type == WeaponType::GRENADE);
	}

	bool ShouldExplode() const {
		return shouldExplode;
	}

	float GetExplodeDuration() const {
    	return explodeDuration;
	}

	void DrawExplosion() const {
        if (shouldExplode) {
            float explosionRadius = 400.f * (1.0f - (explodeDuration / 0.2f));
            DrawCircleLines(transform.position.x, transform.position.y, explosionRadius, ORANGE);
        }
    }

	bool Update(float dt) {
		transform.position = Vector2Add(transform.position, Vector2Scale(physics.velocity, dt));

		//*-* cały if
		if (type == WeaponType::GRENADE) {
			explodeTimer -= dt;

			physics.velocity = Vector2Scale(physics.velocity, 0.95f);

			if (Vector2Length(physics.velocity) < 10.0f || explodeTimer <= 0.0f) {
				isGrenadeActive = false;
				if(!shouldExplode) {
					shouldExplode = true;
					explodeDuration = 0.2f;
				}
			}
		}

		if (type == WeaponType::GRENADE) {
            return false;
        }

		if (transform.position.x < 0 ||
			transform.position.x > Renderer::Instance().Width() ||
			transform.position.y < 0 ||
			transform.position.y > Renderer::Instance().Height())
		{
			return true;
		}
		return false;
	}

	bool ShouldRemove() const {
        if (type == WeaponType::GRENADE && shouldExplode) {
            return explodeDuration <= 0.0f;
        }
        return false;
    }

	void UpdateExplosion(float dt) {
        if (shouldExplode) {
            explodeDuration -= dt;
        }
    }

	void Draw() const {
		if (type == WeaponType::BULLET) {
			DrawCircleV(transform.position, 5.f, WHITE);
		}
		else if (type == WeaponType::LASER) {
			static constexpr float LASER_LENGTH = 30.f;
			Rectangle lr = { transform.position.x - 2.f, transform.position.y - LASER_LENGTH, 4.f, LASER_LENGTH };
			DrawRectangleRec(lr, BLUE);
		}
		else if (type == WeaponType::GRENADE) { //*-* cały if
			DrawCircleV(transform.position, 8.f, RED);
		}
		else if (type == WeaponType::TRISHOT) { //*-* cały if
			DrawCircleV(transform.position, 4.f, YELLOW);
		}
	}
	Vector2 GetPosition() const {
		return transform.position;
	}

	float GetRadius() const {
		if (type == WeaponType::BULLET) return 5.f; //*-*
		if (type == WeaponType::GRENADE) return 8.f; //*-*
		if (type == WeaponType::TRISHOT) return 4.f; //*-*
		return 2.f;
	}

	int GetDamage() const {
		return baseDamage;
	}
	WeaponType GetType() const { return type; } //*-*

	bool IsGrenadeActive() const { //*-*
		return type == WeaponType::GRENADE && isGrenadeActive;
	}

private:
	bool isGrenadeActive; //*-*
	bool shouldExplode = false; //*-*
	float explodeDuration = 0.0f; //*-*
	TransformA transform;
	Physics    physics;
	int        baseDamage;
	WeaponType type;
	float explodeTimer = 0.f; //*-*
};

inline static std::vector<Projectile> MakeProjectile(WeaponType wt,
	const Vector2 pos,
	float speed)
{
	std::vector<Projectile> result;

	Vector2 vel{ 0, -speed };
	if (wt == WeaponType::LASER) {
		result.emplace_back(pos, vel, 20, wt); //*-*
	}
	else if (wt == WeaponType::BULLET) {
		result.emplace_back(pos, vel, 10, wt); //*-*
	}
	else if (wt == WeaponType::GRENADE) { //*-*
		result.emplace_back(pos, Vector2Rotate(vel, 0.7f), 0, wt); // 0 bo eksplozja robi damage
	}
	else if (wt == WeaponType::TRISHOT) { //*-*
		result.emplace_back(pos, vel, 10, wt);
		result.emplace_back(pos, Vector2Rotate(vel, DEG2RAD * 15), 10, wt);
		result.emplace_back(pos, Vector2Rotate(vel, DEG2RAD * -15), 10, wt);
	}
	return result;
}

// --- SHIP HIERARCHY ---
class Ship {
public:
	Ship(int screenW, int screenH) {
		transform.position = {
												 screenW * 0.5f,
												 screenH * 0.5f
		};
		hp = 100;
		speed = 250.f;
		alive = true;

		// per-weapon fire rate & spacing
		fireRateLaser = 18.f; // shots/sec
		fireRateBullet = 22.f;
		spacingLaser = 40.f; // px between lasers
		spacingBullet = 20.f;
	}
	virtual ~Ship() = default;
	virtual void Update(float dt) = 0;
	virtual void Draw() const = 0;

	void TakeDamage(int dmg) {
		if (!alive) return;
		hp -= dmg;
		if (hp <= 0) alive = false;
	}

	bool IsAlive() const {
		return alive;
	}

	Vector2 GetPosition() const {
		return transform.position;
	}

	virtual float GetRadius() const = 0;

	int GetHP() const {
		return hp;
	}

	float GetFireRate(WeaponType wt) const {
		return (wt == WeaponType::LASER) ? fireRateLaser : fireRateBullet;
	}

	float GetSpacing(WeaponType wt) const {
		return (wt == WeaponType::LASER) ? spacingLaser : spacingBullet;
	}

protected:
	TransformA transform;
	int        hp;
	float      speed;
	bool       alive;
	float      fireRateLaser;
	float      fireRateBullet;
	float      spacingLaser;
	float      spacingBullet;
};

class PlayerShip :public Ship {
public:
	PlayerShip(int w, int h) : Ship(w, h) {
		texture = LoadTexture("spaceship1.png");
		GenTextureMipmaps(&texture);                                                        // Generate GPU mipmaps for a texture
		SetTextureFilter(texture, 2);
		scale = 0.25f;
	}
	~PlayerShip() {
		UnloadTexture(texture);
	}

	void Update(float dt) override {
		if (alive) {
			if (IsKeyDown(KEY_W)) transform.position.y -= speed * dt;
			if (IsKeyDown(KEY_S)) transform.position.y += speed * dt;
			if (IsKeyDown(KEY_A)) transform.position.x -= speed * dt;
			if (IsKeyDown(KEY_D)) transform.position.x += speed * dt;
		}
		else {
			transform.position.y += speed * dt;
		}
	}

	void Draw() const override {
		if (!alive && fmodf(GetTime(), 0.4f) > 0.2f) return;
		Vector2 dstPos = {
										 transform.position.x - (texture.width * scale) * 0.5f,
										 transform.position.y - (texture.height * scale) * 0.5f
		};
		DrawTextureEx(texture, dstPos, 0.0f, scale, WHITE);
	}

	float GetRadius() const override {
		return (texture.width * scale) * 0.5f;
	}

private:
	Texture2D texture;
	float     scale;
};

// --- APPLICATION ---
class Application {
public:
	static Application& Instance() {
		static Application inst;
		return inst;
	}

	void Run() {
		srand(static_cast<unsigned>(time(nullptr)));
		Renderer::Instance().Init(C_WIDTH, C_HEIGHT, "Asteroids OOP");

		auto player = std::make_unique<PlayerShip>(C_WIDTH, C_HEIGHT);

		float spawnTimer = 0.f;
		float spawnInterval = Utils::RandomFloat(C_SPAWN_MIN, C_SPAWN_MAX);
		WeaponType currentWeapon = WeaponType::LASER;
		float shotTimer = 0.f;

		while (!WindowShouldClose()) {
			float dt = GetFrameTime();
			spawnTimer += dt;

			// Update player
			player->Update(dt);

			// Restart logic
			if (!player->IsAlive() && IsKeyPressed(KEY_R)) {
				player = std::make_unique<PlayerShip>(C_WIDTH, C_HEIGHT);
				asteroids.clear();
				projectiles.clear();
				spawnTimer = 0.f;
				spawnInterval = Utils::RandomFloat(C_SPAWN_MIN, C_SPAWN_MAX);
			}
			// Asteroid shape switch
			if (IsKeyPressed(KEY_ONE)) {
				currentShape = AsteroidShape::TRIANGLE;
			}
			if (IsKeyPressed(KEY_TWO)) {
				currentShape = AsteroidShape::SQUARE;
			}
			if (IsKeyPressed(KEY_THREE)) {
				currentShape = AsteroidShape::PENTAGON;
			}
			if (IsKeyPressed(KEY_FOUR)) {
				currentShape = AsteroidShape::RANDOM;
			}

			// Weapon switch
			if (IsKeyPressed(KEY_TAB)) {
				currentWeapon = static_cast<WeaponType>((static_cast<int>(currentWeapon) + 1) % static_cast<int>(WeaponType::COUNT));
			}

			// Shooting
			{
				if (player->IsAlive() && IsKeyDown(KEY_SPACE)) {
					shotTimer += dt;
					float interval = 1.f / player->GetFireRate(currentWeapon);
					float projSpeed = player->GetSpacing(currentWeapon) * player->GetFireRate(currentWeapon);

					bool canShoot = true;
					if (currentWeapon == WeaponType::GRENADE) {
						canShoot = (GetTime() - lastGrenadeTime >= GRENADE_COOLDOWN);
					}

					if (canShoot) {
						while (shotTimer >= interval) {
							Vector2 p = player->GetPosition();
							p.y -= player->GetRadius();
							auto newProjs = MakeProjectile(currentWeapon, p, projSpeed); //*-*
							projectiles.insert(projectiles.end(), newProjs.begin(), newProjs.end()); //*-*
							//projectiles.push_back(MakeProjectile(currentWeapon, p, projSpeed));
							
							if (currentWeapon == WeaponType::GRENADE) {
                    			lastGrenadeTime = GetTime();
                			}
							
							shotTimer -= interval;
						}	
					}
				}
				else {
					float maxInterval = 1.f / player->GetFireRate(currentWeapon);

					if (shotTimer > maxInterval) {
						shotTimer = fmodf(shotTimer, maxInterval);
					}
				}
			}

			// Spawn asteroids
			if (spawnTimer >= spawnInterval && asteroids.size() < MAX_AST) {
				asteroids.push_back(MakeAsteroid(C_WIDTH, C_HEIGHT, currentShape));
				spawnTimer = 0.f;
				spawnInterval = Utils::RandomFloat(C_SPAWN_MIN, C_SPAWN_MAX);
			}

			// Update projectiles - check if in boundries and move them forward
			{
				auto projectile_to_remove = std::remove_if(projectiles.begin(), projectiles.end(),
					[dt](auto& projectile) {
						return projectile.Update(dt);
					});
				projectiles.erase(projectile_to_remove, projectiles.end());
			}

			// Projectile-Asteroid collisions O(n^2)
			for (auto pit = projectiles.begin(); pit != projectiles.end();) {
				bool removed = false;

				for (auto ait = asteroids.begin(); ait != asteroids.end(); ++ait) {
					float dist = Vector2Distance((*pit).GetPosition(), (*ait)->GetPosition());
					if (dist < (*pit).GetRadius() + (*ait)->GetRadius()) {
						ait = asteroids.erase(ait);
						pit = projectiles.erase(pit);
						removed = true;
						break;
					}
				}
				if (!removed) {
					++pit;
				}
			}

			{
				for (auto& proj : projectiles) {
					proj.Update(dt);
					if (proj.GetType() == WeaponType::GRENADE && proj.ShouldExplode()) {
						proj.UpdateExplosion(dt);
					}
				}

				auto grenade_to_remove = std::remove_if(projectiles.begin(), projectiles.end(),
					[](auto& projectile) {
						return projectile.ShouldRemove();
					});
				projectiles.erase(grenade_to_remove, projectiles.end());
			}
			
			{ //*-* cały blok
				auto pit = projectiles.begin();
				while (pit != projectiles.end()) {
					if (pit->ShouldExplode()) {
						Vector2 boomPos = pit->GetPosition();
						float explosionRadius = 400.f;
						
						pit->DrawExplosion();

						if (pit->GetExplodeDuration() >= 0.18f) {
							auto ait = asteroids.begin();
							while (ait != asteroids.end()) {
								float dist = Vector2Distance(boomPos, (*ait)->GetPosition());
								if (dist < explosionRadius) {
									ait = asteroids.erase(ait);
								}
								else {
									++ait;
								}
							}
						}
					}
					++pit;
				}
			}

			// Asteroid-Ship collisions
			{
				auto remove_collision =
					[&player, dt](auto& asteroid_ptr_like) -> bool {
					if (player->IsAlive()) {
						float dist = Vector2Distance(player->GetPosition(), asteroid_ptr_like->GetPosition());

						if (dist < player->GetRadius() + asteroid_ptr_like->GetRadius()) {
							player->TakeDamage(asteroid_ptr_like->GetDamage());
							return true; // Mark asteroid for removal due to collision
						}
					}
					if (!asteroid_ptr_like->Update(dt)) {
						return true;
					}
					return false; // Keep the asteroid
					};
				auto asteroid_to_remove = std::remove_if(asteroids.begin(), asteroids.end(), remove_collision);
				asteroids.erase(asteroid_to_remove, asteroids.end());
			}

			// Render everything
			{
				Renderer::Instance().Begin();

				DrawText(TextFormat("HP: %d", player->GetHP()),
					10, 10, 20, GREEN);

				const char* weaponName = "";
				switch(currentWeapon) { //*-*
					case WeaponType::LASER: weaponName = "LASER"; break;
					case WeaponType::BULLET: weaponName = "BULLET"; break;
					case WeaponType::GRENADE: weaponName = "GRENADE"; break;
					case WeaponType::TRISHOT: weaponName = "TRISHOT"; break;
					default: weaponName = "UNKNOWN"; break;
				}
				DrawText(TextFormat("Weapon: %s", weaponName),
					10, 40, 20, BLUE);

				for (const auto& projPtr : projectiles) {
					projPtr.Draw();
				}
				for (const auto& astPtr : asteroids) {
					astPtr->Draw();
				}
				if (currentWeapon == WeaponType::GRENADE) {
					float cooldownLeft = GRENADE_COOLDOWN - (GetTime() - lastGrenadeTime);
					if (cooldownLeft > 0) {
						DrawText(TextFormat("Grenade cooldown: %.1fs", cooldownLeft), 
								10, 70, 20, ORANGE);
					} else {
						DrawText("Grenade ready!", 10, 70, 20, GREEN);
					}
				}

				player->Draw();

				Renderer::Instance().End();
			}
		}
	}

private:
	Application()
	{
		asteroids.reserve(1000);
		projectiles.reserve(10'000);
	};

	std::vector<std::unique_ptr<Asteroid>> asteroids;
	std::vector<Projectile> projectiles;

	AsteroidShape currentShape = AsteroidShape::TRIANGLE;

	static constexpr int C_WIDTH = 1000;
	static constexpr int C_HEIGHT = 1000;
	static constexpr size_t MAX_AST = 150;
	static constexpr float C_SPAWN_MIN = 0.5f;
	static constexpr float C_SPAWN_MAX = 3.0f;

	static constexpr int C_MAX_ASTEROIDS = 1000;
	static constexpr int C_MAX_PROJECTILES = 10'000;
	float lastGrenadeTime = -10.0f; //*-*
	static constexpr float GRENADE_COOLDOWN = 10.0f;//*-*
};

int main() {
	Application::Instance().Run();
	return 0;
}
