**Linux Task Manager (LTM)** <br />
My unholy crusade to build a task manager that doesn’t suck.

Why did I make it? Simple. Every other task manager out there is either slow, lying to you, or about as informative as a fortune cookie written by a toaster. I got tired of watching my system monitor have an existential crisis every time a process sneezed.

So I built my own. It’s written in C++11, multithreaded, and optimized like it’s being chased by the IRS.

What does that mean in human terms? It means this thing is *fast as hell*. It boots basically instantly, can refresh stats every 50 milliseconds, and somehow still uses less CPU than a blinking cursor. The whole executable is only about 150KB, which means it’s smaller than most websites’ cookie trackers.

If you try to launch a second copy, it doesn’t panic and duplicate itself like a gremlin after midnight, it just redirects you to the one that’s already running.

And because it’s multithreaded, the UI doesn’t freeze, lock up, or stare into the void when something heavy happens. It keeps moving, keeps updating, and keeps telling you exactly what your machine is doing without the drama.

In short: it’s a task manager that actually does its damn job.
