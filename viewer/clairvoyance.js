// Axel '0vercl0k' Souchet - December 18 2020
class Clairvoyance_t {
    constructor(Canvas, ClickLog, MouseLog) {

        //
        // The log DOM element is where we can show to the user
        // the virtual-address and the coordinates.
        //

        this.ClickLog_ = ClickLog;
        this.MouseLog_ = MouseLog;

        //
        // The canvas is where we render the pixels.
        //

        this.Canvas_ = Canvas;
        this.Ctx_ = this.Canvas_.getContext('2d');

        //
        // Width, Height of the canvas.
        //

        this.Width_ = undefined;
        this.Height_ = undefined;

        //
        // Order of the Hilbert curve.
        //

        this.Order_ = undefined;

        //
        // This stores the last pixel clicked; it allows us
        // to restore its color when clicking somewhere else.
        //

        this.PixelClick_ = {
            X: undefined,
            Y: undefined,
            Color: {
                R: undefined,
                G: undefined,
                B: undefined,
                A: undefined
            },
            HiColor: {
                R: 0x08,
                G: 0x25,
                B: 0x67,
                A: 0xFF
            }
        };

        //
        // This stores the last pixel mouseover'd; it allows us
        // to restore its original color when moving the mouse
        // somewhere else.
        //

        this.PixelMouseOver_ = {
            X: undefined,
            Y: undefined,
            Color: {
                R: undefined,
                G: undefined,
                B: undefined,
                A: undefined
            },
            HiColor: {
                R: 0xFF,
                G: 0xFF,
                B: 0xFF,
                A: 0xFF
            }
        };

        //
        // Initialize the palette of colors; in order:
        // None, Black
        // UserRead, PaleGreen
        // UserReadExec, CanaryYellow
        // UserReadWrite, Mauve,
        // UserReadWriteExec, LightRed
        // KernelRead, Green
        // KernelReadExec, Yellow
        // KernelReadWrite, Purple
        // KernelReadWriteExec, Red
        //

        this.Palette_ = new Map();
        this.Palette_.set(0, {
            R: 0,
            G: 0,
            B: 0
        });
        this.Palette_.set(1, {
            R: 0xa9,
            G: 0xff,
            B: 0x52
        });
        this.Palette_.set(2, {
            R: 0xff,
            G: 0xff,
            B: 0x99
        });
        this.Palette_.set(3, {
            R: 0xe0,
            G: 0xb0,
            B: 0xff
        });
        this.Palette_.set(4, {
            R: 0xff,
            G: 0x7f,
            B: 0x7f
        });
        this.Palette_.set(5, {
            R: 0x00,
            G: 0xff,
            B: 0x00
        });
        this.Palette_.set(6, {
            R: 0xff,
            G: 0xff,
            B: 0x00
        });
        this.Palette_.set(7, {
            R: 0xa0,
            G: 0x20,
            B: 0xf0
        });
        this.Palette_.set(8, {
            R: 0xfe,
            G: 0x00,
            B: 0x00
        });

        this.Regions_ = [];
    }

    //
    // This is code that I stole from "Hacker's Delight" figure 16â€“8.
    // Calculate the distance from a set of (X, Y) coordinate.
    //

    xy2d(X_, Y_) {
        let[X,Y,S] = [X_, Y_, 0];
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
    // This is code that I stole from "Hacker's Delight" figure 16â€“8.
    // Calculate (X, Y) coordinates from a distance.
    //

    d2xy(Dist) {
        let[S,Sr,Cs,Swap,Comp,T] = [0, 0, 0, 0, 0, 0];
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
        return {
            X: S >> 16,
            Y: S & 0xFFFF
        };
    }

    //
    // Parse a clairvoyance file.
    //

    parseFile(Content) {
        const Lines = Content.split('\n');

        //
        // The header contains the width / height in pixels.
        //

        [this.Width_,this.Height_] = Lines[0].split(' ').map(Number);

        //
        // Set the canvas' dimensions.
        //

        this.Canvas_.width = this.Width_;
        this.Canvas_.height = this.Height_;

        //
        // Calculate the order of the curve.
        //

        this.Order_ = Math.log2(this.Width_);
        const ImgData = this.Ctx_.getImageData(0, 0, this.Width_, this.Height_);
        let Distance = 0;

        //
        // Walk the lines; either it specifies the protection
        // of a page, or it specifies the address of the current
        // region.
        //

        for (const Line of Lines.slice(1)) {

            //
            // If it starts with '0x', this is the address of the current
            // region.
            //

            if (Line.startsWith('0x')) {

                //
                // If we have a new region we keep track of its start.
                //

                this.Regions_.push({
                    Va: BigInt(Line),
                    Start: BigInt(Distance),
                });

                continue;
            }

            //
            // The line is a page protection. In order to color the pixel,
            // we need to calculate which pixel that is on the canvas using
            // its distance.
            //

            const Coord = this.d2xy(Distance);

            //
            // Once we have the coordinates, we can calculate its offset as well
            // as its color.
            //

            const Color = this.Palette_.get(Number(Line));
            const Offset = ((Coord.Y * this.Width_) + Coord.X) * 4;
            ImgData.data[Offset + 0] = Color.R;
            ImgData.data[Offset + 1] = Color.G;
            ImgData.data[Offset + 2] = Color.B;
            ImgData.data[Offset + 3] = 0xff;
            Distance++;

            //
            // If the clairvoyance file specifies more pages that fit onto the
            // curve, we bail.
            // XXX: Technically here, I think we should create a second canvas
            // to be able to represent the rest of the pixels. It could be shown
            // to the user as a 'second' page like in a book for example.
            //

            if (Distance == (this.Width_ * this.Height_)) {
                break;
            }
        }

        //
        // Update the canvas' content.
        //

        this.Ctx_.putImageData(ImgData, 0, 0);

        //
        // Fix up its style and define the events we'll handle.
        //

        this.Canvas_.style = 'image-rendering: pixelated';
        this.Canvas_.onclick = Event=>{
            const X = Event.offsetX;
            const Y = Event.offsetY;
            this.click(X, Y);
        }
        ;

        this.Canvas_.onmousemove = Event=>{
            const X = Event.offsetX;
            const Y = Event.offsetY;
            this.mouseMove(X, Y);
        }
        ;
    }

    //
    // Calculate the address from coordinates.
    //

    addressFromCoord(X, Y) {
        const Distance = BigInt(this.xy2d(X, Y));
        for (const [Idx,Region] of this.Regions_.entries()) {
            const NextRegion = this.Regions_[Idx + 1];
            if (NextRegion == undefined || Distance < NextRegion.Start) {
                const Va = Region.Va + ((Distance - Region.Start) * 0x1000n);
                return Va;
            }
        }

        return undefined;
    }

    //
    // Set a pixel to a specific color.
    //

    setPixelColor(ImgData, X, Y, Color) {

        //
        // Calculate the offset of the pixel.
        //

        const Offset = ((Y * this.Width_) + X) * 4;
        ImgData.data[Offset + 0] = Color.R;
        ImgData.data[Offset + 1] = Color.G;
        ImgData.data[Offset + 2] = Color.B;
        ImgData.data[Offset + 3] = Color.A;
    }

    //
    // Save a pixel color.
    //

    savePixelColor(ImgData, X, Y) {

        //
        // Calculate the offset of the pixel.
        //

        const Offset = ((Y * this.Width_) + X) * 4;
        return {
            R: ImgData.data[Offset + 0],
            G: ImgData.data[Offset + 1],
            B: ImgData.data[Offset + 2],
            A: ImgData.data[Offset + 3],
        };
    }

    //
    // Highlight a clicked pixel.
    //

    click(X, Y) {

        //
        // Calculate the virtual address at this point.
        //

        const Va = this.addressFromCoord(X, Y);
        if (Va == undefined) {
            throw `addressFromCoord failed`;
        }

        //
        // Get the canvas' content.
        //

        const ImgData = this.Ctx_.getImageData(0, 0, this.Width_, this.Height_);

        //
        // Restore the old pixel's color.
        //

        this.setPixelColor(ImgData, this.PixelClick_.X, this.PixelClick_.Y, this.PixelClick_.Color);

        //
        // Save the one we're about to highlight. It's trickier than one would think though.
        // The issue is that because we track the mouse moving, there's a good chance that
        // the current pixel is being highlighted; in which case we would read the highlight
        // color instead of its origin color.
        // The trick is to check if the current position equals the one of the last move over,
        // in which case we steal the color from there.
        //

        this.PixelClick_.X = X;
        this.PixelClick_.Y = Y;
        if (this.PixelMouseOver_.X == X && this.PixelMouseOver_.Y == Y) {
            this.PixelClick_.Color = this.PixelMouseOver_.Color;
        } else {
            this.PixelClick_.Color = this.savePixelColor(ImgData, X, Y);
        }

        //
        // Change the pixel color.
        //

        this.setPixelColor(ImgData, X, Y, this.PixelClick_.HiColor);

        //
        // Update the canvas' content.
        //

        this.Ctx_.putImageData(ImgData, 0, 0);

        //
        // Refresh the text log.
        //

        this.ClickLog_.innerText = `${Va.toString(16)}`;
    }

    //
    // Highlight a pixel when the mouse is moving.
    //

    mouseMove(X, Y) {

        //
        // Calculate the virtual address at this point.
        //

        const Va = this.addressFromCoord(X, Y);
        if (Va == undefined) {
            throw `addressFromCoord failed`;
        }

        //
        // Get the canvas' content.
        //

        const ImgData = this.Ctx_.getImageData(0, 0, this.Width_, this.Height_);

        //
        // Carry on with highlighting only if this pixel hasn't been clicked on,
        // otherwise it means we would remove its color.
        //

        if (this.PixelMouseOver_.X != this.PixelClick_.X || this.PixelMouseOver_.Y != this.PixelClick_.Y) {

            //
            // Restore the old pixel's color.
            //

            this.setPixelColor(ImgData, this.PixelMouseOver_.X, this.PixelMouseOver_.Y, this.PixelMouseOver_.Color);
        }

        if (X != this.PixelClick_.X || Y != this.PixelClick_.Y) {

            //
            // Save the one we're about to highlight.
            //

            this.PixelMouseOver_.X = X;
            this.PixelMouseOver_.Y = Y;
            this.PixelMouseOver_.Color = this.savePixelColor(ImgData, X, Y);

            //
            // Change the pixel color if we haven't clicked on it.
            //

            this.setPixelColor(ImgData, X, Y, this.PixelMouseOver_.HiColor);
        }

        //
        // Update the canvas' content.
        //

        this.Ctx_.putImageData(ImgData, 0, 0);

        //
        // Refresh the text log.
        //

        this.MouseLog_.innerText = `${Va.toString(16)}`;
    }
}
