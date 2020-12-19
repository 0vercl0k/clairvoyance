// Axel '0vercl0k' Souchet - December 18 2020
class Clairvoyance_t {
    constructor(Canvas) {
        this.Canvas_ = Canvas;
        this.Ctx_ = this.Canvas_.getContext('2d');
        this.Width_ = undefined;
        this.Height_ = undefined;
        this.Order_ = undefined;
        this.PixelClick_ = {
            Offset: undefined,
            R: undefined,
            G: undefined,
            B: undefined,
            A: undefined,
            Color: {
                R: 0xFF,
                G: 0x00,
                B: 0x00,
                A: 0xFF
            }
        };

        this.PixelMouseOver_ = {
            Offset: undefined,
            R: undefined,
            G: undefined,
            B: undefined,
            A: undefined,
            Color: {
                R: 0xFF,
                G: 0xFF,
                B: 0xFF,
                A: 0xFF
            }
        };

        this.Palette_ = new Map();
        // None, Black
        this.Palette_.set(0, {R: 0, G: 0, B: 0});
        // UserRead, PaleGreen
        this.Palette_.set(1, {R: 0xa9, G: 0xff, B: 0x52});
        // UserReadExec, CanaryYellow
        this.Palette_.set(2, {R: 0xff, G: 0xff, B: 0x99});
        // UserReadWrite, Mauve,
        this.Palette_.set(3, {R: 0xe0, G: 0xb0, B: 0xff});
        // UserReadWriteExec, LightRed
        this.Palette_.set(4, {R: 0xff, G: 0x7f, B: 0x7f});
        // KernelRead, Green
        this.Palette_.set(5, {R: 0x00, G: 0xff, B: 0x00});
        // KernelReadExec, Yellow
        this.Palette_.set(6, {R: 0xff, G: 0xff, B: 0x00});
        // KernelReadWrite, Purple
        this.Palette_.set(7, {R: 0xa0, G: 0x20, B: 0xf0});
        // KernelReadWriteExec, Red
        this.Palette_.set(8, {R: 0xfe, G: 0x00, B: 0x00});
        this.Regions_ = [];
    }

    //
    // This is code that I stole from "Hacker's Delight" figure 16–8.
    //

    xy2d(X_, Y_) {
        let [X, Y, S] = [X_, Y_, 0];
        for (let Idx = this.Order_ - 1; Idx >= 0; Idx--) {
          const Xi = (X >> Idx) & 1;
          const Yi = (Y >> Idx) & 1;
          if (Yi == 0) {
            const Tmp = X;
            X = Y ^ (-Xi);
            Y = Tmp ^ (-Xi);
          }
          S = 4 * S + 2 * Xi + (Xi ^ Yi);
        }

        return S;
    }

    //
    // This is code that I stole from "Hacker's Delight" figure 16–8.
    //

    d2xy(Dist) {
        let [S, Sr, Cs, Swap, Comp, T] = [0, 0, 0, 0, 0, 0];
        S = Dist | (0x55555555 << 2 * this.Order_);
        Sr = (S >> 1) & 0x55555555;
        Cs = ((S & 0x55555555) + Sr) ^ 0x55555555;
        Cs ^= (Cs >> 2);
        Cs ^= (Cs >> 4);
        Cs ^= (Cs >> 8);
        Cs ^= (Cs >> 16);
        Swap = Cs & 0x55555555;
        Comp = (Cs >> 1) & 0x55555555;
        T = (S & Swap) ^ Comp;
        S ^= Sr ^ T ^ (T << 1);
        S &= (1 << (2 * this.Order_)) - 1;
        T = (S ^ (S >> 1)) & 0x22222222;
        S ^= T ^ (T << 1);
        T = (S ^ (S >> 2)) & 0x0C0C0C0C;
        S ^= T ^ (T << 2);
        T = (S ^ (S >> 4)) & 0x00F000F0;
        S ^= T ^ (T << 4);
        T = (S ^ (S >> 8)) & 0x0000FF00;
        S ^= T ^ (T << 8);
        const Coord = {
            X: S >> 16,
            Y: S & 0xFFFF
        };
        debugger;
        return Coord;
    }

    parseFile(Content) {
        const Lines = Content.split('\n');
        const Split = Lines[0].split(' ');
        this.Width_ = Number(Split[0]);
        this.Height_ = Number(Split[1]);
        this.Order_ = Math.log2(this.Width_);
        this.Canvas_.width = this.Width_;
        this.Canvas_.height = this.Height_;
        const ImgData = this.Ctx_.getImageData(0, 0, this.Width_, this.Height_);
        let Distance = 0;
        let Va = 0n
        for(const Line of Lines.slice(1)) {
          if(Line.startsWith('0x')) {
            this.Regions_.push({
              Va: BigInt(Line),
              Start: BigInt(Distance),
            });
            Va = BigInt(Line);
            continue;
          }

          const Coord = this.d2xy(Distance);
          const Color = this.Palette_.get(Number(Line));
          const Offset = ((Coord.Y * this.Width_) + Coord.X) * 4;
          ImgData.data[Offset + 0] = Color.R;
          ImgData.data[Offset + 1] = Color.G;
          ImgData.data[Offset + 2] = Color.B;
          ImgData.data[Offset + 3] = 0xff;
          Distance++;
          Va += 0x1000n;
          if(Distance == (this.Width_ * this.Height_)) {
            break;
          }
        }

        this.Ctx_.putImageData(ImgData, 0, 0);
        this.Canvas_.style = 'border:1px solid #d3d3d3';
        this.Canvas_.onclick = Event => {
            this.highlightPixel(Event.offsetX, Event.offsetY, this.PixelClick_);
        };

        this.Canvas_.onmousemove = Event => {
            this.highlightPixel(Event.offsetX, Event.offsetY, this.PixelMouseOver_);
        };
    }

    addressFromCoord(X, Y) {
        const Distance = BigInt(this.xy2d(X, Y));
        for(const [Idx, Region] of this.Regions_.entries()) {
            const NextRegion = this.Regions_[Idx + 1];
            if(NextRegion == undefined || Distance < NextRegion.Start) {
                const Va = Region.Va + ((Distance - Region.Start) * 0x1000n);
                return Va;
            }
        }

        return undefined;
    }

    highlightPixel(X, Y, Pixel) {
        const Va = this.addressFromCoord(X, Y);
        if(Va == undefined) {
            throw `Region not found`;
        }

        const ImgData = this.Ctx_.getImageData(0, 0, this.Width_, this.Height_);
        const Offset = ((Y * this.Width_) + X) * 4;

        //
        // Restore the old pixel's color.
        //

        ImgData.data[Pixel.Offset + 0] = Pixel.R;
        ImgData.data[Pixel.Offset + 1] = Pixel.G;
        ImgData.data[Pixel.Offset + 2] = Pixel.B;
        ImgData.data[Pixel.Offset + 3] = Pixel.A;

        //
        // Save the one we're about to highlight.
        //

        Pixel.Offset = Offset;
        Pixel.R = ImgData.data[Offset + 0];
        Pixel.G = ImgData.data[Offset + 1];
        Pixel.B = ImgData.data[Offset + 2];
        Pixel.A = ImgData.data[Offset + 3];

        //
        // Highlight the pixel.
        //

        ImgData.data[Offset + 0] = Pixel.Color.R;
        ImgData.data[Offset + 1] = Pixel.Color.G;
        ImgData.data[Offset + 2] = Pixel.Color.B;
        ImgData.data[Offset + 3] = Pixel.Color.A;
        this.Ctx_.putImageData(ImgData, 0, 0);
        document.getElementById('text').innerText = `VA=${Va.toString(16)} (d=${this.xy2d(X, Y)} x=${X}, y=${Y})`;
    }
};